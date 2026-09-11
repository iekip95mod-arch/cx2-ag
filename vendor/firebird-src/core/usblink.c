#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "emu.h"
#include "usb.h"
#include "usb_cx2.h"
#include "usblink.h"
#include "usblink_cx2.h"
#include "os/os.h"

struct packet {
    uint16_t constant;
    struct { uint16_t addr, service; } src;
    struct { uint16_t addr, service; } dst;
    uint16_t data_check;
    uint8_t data_size; // If 0xFF, bigdata* counts
    uint8_t ack;
    uint8_t seqno;
    uint8_t hdr_check;
    union {
        uint8_t      data[254];
        struct {
            uint32_t bigdatasize;
            uint8_t  bigdata[1440];
        };
        uint8_t      fulldata[1444];
    };
};

#define CONSTANT  BSWAP16(0x54FD)
#define SRC_ADDR  BSWAP16(0x6400)
#define DST_ADDR  BSWAP16(0x6401)

enum SID {
    SID_File = 0x8001,
    SID_Dirlist,
    SID_Raw
};

enum File_Action {
    File_Put = 0x03,
    File_Ready = 0x04,
    File_Contents = 0x05,
    File_Get = 0x07,
    File_Del = 0x09,
    File_New_Folder = 0x0A,
    File_Del_Folder = 0x0B,
    File_Copy = 0x0C,
    File_Dirlist_Init = 0x0D,
    File_Dirlist_Next = 0x0E,
    File_Dirlist_Done = 0x0F,
    File_Dirlist_Entry = 0x10,
    File_Attr = 0x20,
    File_Rename = 0x21
};

enum USB_Mode {
    File_Send = 0,
    Dirlist,
    Rename,
    Delete,
    File_Receive,
    Dir_Create,
    Raw,
    Key
} mode;

static uint8_t *packet_dataptr(struct packet *p) {
    return (p->data_size == 0xFF) ? p->bigdata : p->data;
}

static uint32_t packet_datasize(const struct packet *p) {
    return (p->data_size == 0xFF) ? BSWAP32(p->bigdatasize) : p->data_size;
}

static uint32_t packet_max_datasize() {
    // TODO: Would be 1440 for NNSE, but usb_cx2 only handles 1023 per transfer max.
    return emulate_cx2 ? (1023-4-12-16) : 254;
}

// Sets the size and returns a pointer where to store the data.
static uint8_t *packet_prepare(struct packet *p, size_t size) {
    if(size <= 254) {
        // Regular small packet
        p->data_size = size;
        return p->data;
    }

    if(size <= 1440 && emulate_cx2) {
        // Big packet
        p->data_size = 0xFF;
        p->fulldata[0] = size >> 24;
        p->fulldata[1] = size >> 16;
        p->fulldata[2] = size >> 8;
        p->fulldata[3] = size;
        return p->bigdata;
    }

    return NULL;
}

static uint32_t packet_fulldatasize(const struct packet *p) {
        return (p->data_size == 0xFF) ? (BSWAP32(p->bigdatasize) + 4) : p->data_size;
}

uint16_t usblink_data_checksum(struct packet *packet) {
    uint16_t check = 0;
    int i, size = packet_fulldatasize(packet);
    for (i = 0; i < size; i++) {
        uint16_t tmp = check << 12 ^ check << 8;
        check = (packet->fulldata[i] << 8 | check >> 8)
                ^ tmp ^ tmp >> 5 ^ tmp >> 12;
    }
    return BSWAP16(check);
}

uint8_t usblink_header_checksum(struct packet *packet) {
    uint8_t check = 0;
    int i;
    for (i = 0; i < 15; i++) check += ((uint8_t *)packet)[i];
    return check;
}

static void dump_packet(char *type, const void *data, uint32_t size) {
    if (log_enabled[LOG_USB])
    {
        uint32_t i;
        emuprintf("%s", type);
        for (i = 0; i < size; i++)
            emuprintf(" %02x %c", ((uint8_t *)data)[i], isprint(((uint8_t *)data)[i]) ? ((uint8_t *)data)[i] : '?');
        emuprintf("\n");
    }
}

struct packet usblink_send_buffer;
void usblink_send_packet() {
    extern void usblink_start_send();
    usblink_send_buffer.constant   = CONSTANT;
    usblink_send_buffer.src.addr   = SRC_ADDR;
    usblink_send_buffer.dst.addr   = DST_ADDR;
    if (emulate_cx2) usblink_send_buffer.seqno = 0;
    usblink_send_buffer.data_check = usblink_data_checksum(&usblink_send_buffer);
    usblink_send_buffer.hdr_check  = usblink_header_checksum(&usblink_send_buffer);
    dump_packet("send", &usblink_send_buffer, 16 + packet_fulldatasize(&usblink_send_buffer));
    usblink_start_send();
}

uint8_t prev_seqno;
uint8_t next_seqno() {
    prev_seqno = (prev_seqno == 0xFF) ? 0x01 : prev_seqno + 1;
    return prev_seqno;
}

static usblink_dirlist_cb current_dirlist_callback;
static usblink_progress_cb current_file_callback;
static usblink_raw_cb current_raw_callback;
static void *current_user_data = NULL;
static uint32_t current_key;
static unsigned key_stage;
static uint16_t next_host_service = SID_Raw;
static uint16_t current_host_service;
static uint16_t current_service;
static unsigned raw_stage;
static uint8_t raw_reply[1440];
static uint32_t raw_reply_size;
static bool raw_reply_failed;

FILE *put_file = NULL;
// Where a get is writing, so a refused one can take its empty file back with it.
static char get_file_dest[FILENAME_MAX];
uint32_t put_file_size, put_file_size_orig;
uint16_t put_file_port;
enum {
    SENDING_03         = 1,
    RECVING_04         = 2,
    ACKING_04_or_FF_00 = 3,
    RECVING_FF_00      = 4,
    DONE               = 5,
    EXPECT_FF_00       = 16, // Sent to us after the first OS data packet(s)
} put_file_state;

void put_file_next(struct packet *in) {
    struct packet *out = &usblink_send_buffer;
    int16_t status = -1;
    if(in && packet_datasize(in) >= 2)
    {
        memcpy(&status, packet_dataptr(in), sizeof(status));
        status = BSWAP16(status);
    }

    switch (put_file_state & 15) {
        case SENDING_03:
            if (in) goto fail;
            put_file_state++;
            break;
        case RECVING_04:
            if (!in || in->data_size != 1 || in->data[0] != 0x04) {
                emuprintf("File send error: Didn't get 04\n");
                goto fail;
            }

            put_file_state++;
            goto send_data;
        case ACKING_04_or_FF_00:
            if (in) goto fail;
            if (put_file_state & EXPECT_FF_00) {
                put_file_state = RECVING_FF_00;
                break;
            }
send_data:
            if (prev_seqno == 1)
            {
                gui_status_printf("Sending file: %u bytes left", put_file_size);
                throttle_timer_off();
            }
            if (put_file_size > 0) {
                /* Send data (05) */
                uint32_t len = put_file_size;
                const uint32_t maxlen = packet_max_datasize() - 1;
                if (len > maxlen)
                    len = maxlen;
                put_file_size -= len;

                out->src.service = SID_File;
                out->dst.service = put_file_port;
                out->ack = 0;
                out->seqno = next_seqno();
                uint8_t *data = packet_prepare(out, len + 1);
                data[0] = File_Contents;
                (void)fread(data + 1, 1, len, put_file);
                usblink_send_packet();

                if(current_file_callback)
                {
                    static int old_progress = 101;
                    //Not 100 as the completion is signaled seperately and mustn't happen more than once
                    int progress = ((put_file_size_orig-put_file_size) * 99) / put_file_size_orig;
                    if(old_progress != progress)
                        current_file_callback(old_progress = progress, current_user_data);
                }
                break;
            }

            gui_status_printf("Send complete");
            put_file_state = DONE;
            break;
        case RECVING_FF_00:
            if (in && in->data_size == 2 && in->data[0] == 0xFF && !in->data[1]) {
                /* Got FF 00: OS header is valid. Just send data from now on. */
                put_file_state = ACKING_04_or_FF_00;
                goto send_data;
            }
            else if (in && in->data_size == 1 && in->data[0] == 0x04) {
                /* Got 04: Send more data for OS header validation */
                put_file_state = ACKING_04_or_FF_00 | EXPECT_FF_00;
                goto send_data;
            }

            emuprintf("File send error: Didn't get 04 00 or FF 00/\n");
            goto fail;
        case DONE:
            // TODO: 06 XX (OS progress) should be handled somewhere else
            if (!(in && in->data_size == 2 && in->data[0] == 0x06)
                && !(in && in->data_size == 2 && in->data[0] == 0xFF && !in->data[1])){
                emuprintf("File send error: Didn't get FF 00 or 06 XX\n");
                goto fail;
            }

            if(current_file_callback)
                current_file_callback(100, current_user_data);

            goto teardown;
    }

    return;

fail:
    if(current_file_callback)
    {
        if(status >= 0)
            status = -1;

        current_file_callback(status, current_user_data);
        current_file_callback = NULL;
    }
    emuprintf("Send failed\n");
    usblink_connected = false;
    gui_usblink_changed(false);

teardown:
    throttle_timer_on();
    put_file_state = 0;
    if (put_file)
        fclose(put_file);
    put_file = NULL;
}

static void get_file_finish(bool success)
{
    if(fclose(put_file) != 0)
        success = false;
    put_file = NULL;

    struct packet *out = &usblink_send_buffer;
    out->src.service = SID_File;
    out->dst.service = BSWAP16(0x4060);
    out->ack = 0;
    out->seqno = next_seqno();
    out->data[0] = 0xFF;
    out->data[1] = success ? 0x00 : 0x01;
    out->data_size = 2;
    usblink_send_packet();

    usblink_progress_cb callback = current_file_callback;
    current_file_callback = NULL;
    if(callback)
        callback(success ? 100 : -1, current_user_data);
}

void get_file_next(struct packet *in)
{
    if(!in || !put_file)
        return;

    uint32_t size = packet_datasize(in);
    if(size == 0)
        return;
    if(size > (in->data_size == 0xFF ? sizeof(in->bigdata) : sizeof(in->data)))
    {
        get_file_finish(false);
        return;
    }
    uint8_t *data = packet_dataptr(in);

    switch(data[0])
    {
    case 0x03: // Receive data size
        if(in->data_size == 15)
        {
            uint32_t size = 0;
            memcpy(&size, data + 2 + 9, sizeof(uint32_t));
            size = BSWAP32(size);
            put_file_size_orig = size;
            put_file_size = 0;

            if(current_file_callback)
                    current_file_callback(0, current_user_data);

            // Send next packet
            struct packet *out = &usblink_send_buffer;
            out->src.service = SID_File;
            out->dst.service = BSWAP16(0x4060);
            out->ack = 0;
            out->seqno = next_seqno();
            uint8_t *data = out->data;
            *data++ = 0x04;
            out->data_size = data - out->data;
            usblink_send_packet();
        }
        break;

    case 0x05: // Receive data packet
        if(put_file_size > put_file_size_orig || size - 1 > put_file_size_orig - put_file_size)
        {
            get_file_finish(false);
            break;
        }
        if(fwrite(data + 1, 1, size - 1, put_file) != size - 1)
        {
            get_file_finish(false);
            break;
        }
        put_file_size += size - 1;

        if(in->data_size < 254 || put_file_size == put_file_size_orig)
            get_file_finish(put_file_size == put_file_size_orig);
        else if(current_file_callback)
        {
            int progress = ((uint64_t)put_file_size * 99) / put_file_size_orig;
            current_file_callback(progress, current_user_data);
        }
        break;

    case 0xFF:
        // The calculator refuses a get it cannot serve, a missing path being the usual reason. This
        // used to fall through unhandled, so the transfer sat there until the queue's own deadline
        // gave up 20 seconds later with nothing to say about why. A 0xFF arriving after put_file is
        // closed is the trailing status of a transfer that already finished, so ignore that one.
        if(!put_file)
            break;

        fclose(put_file);
        put_file = NULL;
        // Without this the host is left holding an empty file, which reads as a fetch that worked.
        if(get_file_dest[0])
            remove(get_file_dest);
        emuprintf("File receive error: the calculator refused the path, status %02x\n",
                  size >= 2 ? data[1] : 0);
        if(current_file_callback)
            current_file_callback(-1, current_user_data);
        break;
    }
}

static void service_begin(uint16_t sid)
{
    current_service = sid;
    current_host_service = next_host_service;
    next_host_service = next_host_service == UINT16_MAX ? SID_Raw : next_host_service + 1;
}

static void service_close(void)
{
    struct packet *out = &usblink_send_buffer;
    out->src.service = BSWAP16(0x40DE);
    out->dst.service = BSWAP16(current_service);
    out->ack = 0;
    out->seqno = next_seqno();
    out->data_size = 2;
    out->data[0] = current_host_service >> 8;
    out->data[1] = current_host_service & 0xff;
    usblink_send_packet();
}

void usblink_send_key(uint32_t key, usblink_progress_cb callback, void *user_data)
{
    if (key > 0xffffff) {
        if (callback) callback(-1, user_data);
        return;
    }
    mode = Key;
    current_key = key;
    key_stage = 1;
    current_file_callback = callback;
    current_user_data = user_data;
    service_begin(0x4042);
    struct packet *out = &usblink_send_buffer;
    out->src.service = BSWAP16(current_host_service);
    out->dst.service = BSWAP16(0x4042);
    out->ack = 0;
    out->seqno = next_seqno();
    out->data_size = 4;
    memcpy(out->data, "\x01\x00\x00\x80", 4);
    usblink_send_packet();
}

static void key_next(struct packet *in)
{
    if (!key_stage) return;
    if (in) {
        if (in->dst.service != BSWAP16(current_host_service)) return;
        if (in->src.service != BSWAP16(0x00D3)) return;
        key_stage = 0;
        usblink_progress_cb callback = current_file_callback;
        current_file_callback = NULL;
        if (callback) callback(-1, current_user_data);
        return;
    }
    struct packet *out = &usblink_send_buffer;
    if (key_stage == 1) {
        out->src.service = BSWAP16(current_host_service);
        out->dst.service = BSWAP16(0x4042);
        out->data_size = 26;
        memset(out->data, 0, 26);
        out->data[4] = 8;
        out->data[5] = 2;
        out->data[6] = current_key >> 16;
        out->data[8] = current_key >> 8;
        out->data[24] = current_key;
    } else if (key_stage == 2) {
        key_stage = 3;
        service_close();
        return;
    } else {
        key_stage = 0;
        usblink_progress_cb callback = current_file_callback;
        current_file_callback = NULL;
        if (callback) callback(100, current_user_data);
        return;
    }
    ++key_stage;
    out->ack = 0;
    out->seqno = next_seqno();
    usblink_send_packet();
}

void usblink_raw(uint16_t sid, const uint8_t *data, uint32_t size, usblink_raw_cb callback, void *user_data)
{
    mode = Raw;
    current_raw_callback = callback;
    current_user_data = user_data;
    raw_stage = 1;
    raw_reply_failed = false;
    service_begin(sid);

    struct packet *out = &usblink_send_buffer;
    uint8_t *dest = packet_prepare(out, size);
    if (!dest) {
        current_raw_callback = NULL;
        raw_stage = 0;
        if (callback) callback(NULL, 0, true, user_data);
        return;
    }
    out->src.service = BSWAP16(current_host_service);
    out->dst.service = BSWAP16(sid);
    out->ack = 0;
    out->seqno = next_seqno();
    memcpy(dest, data, size);
    usblink_send_packet();
}

static void raw_next(struct packet *in)
{
    if (!raw_stage) return;
    if (!in) {
        if (raw_stage != 2) return;
        raw_stage = 0;
        usblink_raw_cb callback = current_raw_callback;
        current_raw_callback = NULL;
        if (callback)
            callback(raw_reply, raw_reply_size, raw_reply_failed, current_user_data);
        return;
    }
    if (in->dst.service != BSWAP16(current_host_service)) return;
    usblink_raw_cb callback = current_raw_callback;
    if (in->src.service == BSWAP16(0x00D3)) {
        raw_stage = 0;
        current_raw_callback = NULL;
        if (callback)
            callback(NULL, 0, true, current_user_data);
        return;
    }
    if (raw_stage != 1) return;
    raw_reply_size = packet_datasize(in);
    raw_reply_failed = raw_reply_size > sizeof raw_reply;
    if (raw_reply_failed) raw_reply_size = 0;
    else memcpy(raw_reply, packet_dataptr(in), raw_reply_size);
    raw_stage = 2;
    service_close();
}

enum Success_Action {
    Success_Done,
    Success_Next
} dirlist_success_action;

void usblink_dirlist(const char *dir, usblink_dirlist_cb callback, void *user_data)
{
    mode = Dirlist;
    current_dirlist_callback = callback;
    current_user_data = user_data;

    /* Send the first packet */
    struct packet *out = &usblink_send_buffer;
    out->src.service = SID_Dirlist;
    out->dst.service = BSWAP16(0x4060);
    out->ack = 0;
    out->seqno = next_seqno();
    uint8_t *data = out->data;
    memset(data, 0, sizeof(out->data));
    *data++ = File_Dirlist_Init;
    //TODO
    assert(strlen(dir) < sizeof(out->data) - 2);
    data += sprintf((char*) data, "%s", dir) + 1; //0-byte
    out->data_size = data - out->data;
    if(out->data_size < 10)
        out->data_size = 10;

    dirlist_success_action = Success_Next;
    usblink_send_packet();
}

void dirlist_next(struct packet *in)
{
    if(!in)
        return;

    struct packet *out = &usblink_send_buffer;

    if(in->data[0] == 0xFF) // Status message
    {
        switch(in->data[1])
        {
        case 0:
            //Success
            if(dirlist_success_action == Success_Next)
                goto request_next;
            else if(current_dirlist_callback)
            {
                current_dirlist_callback(NULL, false, current_user_data);
                current_dirlist_callback = NULL;
            }

            break;
        case 0x11:
            // It is possible that NavNet answers File_Dirlist_Done with FF 11 somehow.
            // Ignore that..
            if(dirlist_success_action == Success_Done)
                break;

            //Enum done
            out->src.service = SID_Dirlist;
            out->dst.service = BSWAP16(0x4060);
            out->ack = 0;
            out->seqno = next_seqno();
            uint8_t *data = out->data;
            memset(data, 0, sizeof(out->data));
            *data++ = File_Dirlist_Done;
            out->data_size = data - out->data;

            dirlist_success_action = Success_Done;
            usblink_send_packet();
            return;
        default:
            //Error
            if(current_dirlist_callback)
                current_dirlist_callback(NULL, true, current_user_data);
            current_dirlist_callback = NULL;
            gui_debug_printf("usblink error 0x%x\n", in->data[1]);
            return;
        }
    }
    else if(in->data[0] == 0x10) // Enum message
    {
        struct usblink_file file_info;
        file_info.filename = (const char*) in->data + 3;
        file_info.is_dir = !!in->data[in->data_size - 2];
        memcpy(&file_info.size, in->data + (in->data_size - 10), sizeof(uint32_t));
        file_info.size = BSWAP32(file_info.size);

        if(current_dirlist_callback)
            current_dirlist_callback(&file_info, false, current_user_data);

        request_next: //Request next entry
        out->src.service = SID_Dirlist;
        out->dst.service = BSWAP16(0x4060);
        out->ack = 0;
        out->seqno = next_seqno();
        uint8_t *data = out->data;
        memset(data, 0, sizeof(out->data));
        *data++ = File_Dirlist_Next;
        out->data_size = data - out->data;

        dirlist_success_action = Success_Next;
        usblink_send_packet();
    }
}

void usblink_received_packet(const uint8_t *data, uint32_t size) {
    struct packet incoming;
    if (data) {
        if (size < 16) return;
        memcpy(&incoming, data, size < sizeof incoming ? size : sizeof incoming);
        if (incoming.data_size == 0xff && size < 20) return;
        uint32_t declared = packet_datasize(&incoming);
        uint32_t capacity = incoming.data_size == 0xff ? sizeof incoming.bigdata : sizeof incoming.data;
        uint32_t header = incoming.data_size == 0xff ? 20 : 16;
        if (declared > capacity || declared > size - header) return;
    }
    dump_packet("recv", data, size);

    struct packet *in = data ? &incoming : NULL;
    struct packet *out = &usblink_send_buffer;

    // Acks are passed as NULL packets
    if (in && in->ack == 0x0A)
        in = NULL;

    if (in && in->src.service == BSWAP16(0x4003)) { /* Address request */
        gui_status_printf("usblink connected.");
        usblink_connected = true;
        gui_usblink_changed(true);
        out->src.service = BSWAP16(0x4003);
        out->dst.service = BSWAP16(0x4003);
        out->data_size = 4;
        out->ack = 0;
        out->seqno = 1;
        uint16_t tmp = DST_ADDR;
        memcpy(out->data + 0, &tmp, sizeof(tmp)); // *(uint16_t *)&out->data[0] = DST_ADDR;
        tmp = BSWAP16(0xFF00);
        memcpy(out->data + 2, &tmp, sizeof(tmp)); // *(uint16_t *)&out->data[2] = BSWAP16(0xFF00);
        usblink_send_packet();
        return;
    } else if (in && !in->ack && !emulate_cx2) { // CX2 acks are handled on the NNSE layer
        /* Send an ACK */
        out->src.service = BSWAP16(0x00FF);
        out->dst.service = in->src.service;
        out->data_size = 2;
        out->ack = 0x0A;
        out->seqno = in->seqno;
        memcpy(&out->data[0], &in->dst.service, sizeof(in->dst.service)); // *(uint16_t *)&out->data[0] = in->dst.service;
        usblink_send_packet();
    }

    /* Ignore disconnects from the LOGIN service */
    if(in && in->src.service == BSWAP16(0x40DE) && in->dst.service == BSWAP16(0x4050))
        return;

    switch(mode)
    {
    case Rename:
    case Delete:
    case Dir_Create:
        if(in && packet_datasize(in) == 2 && packet_dataptr(in)[0] == 0xFF && current_file_callback)
            current_file_callback(packet_dataptr(in)[1] == 0x00 ? 100 : -1, current_user_data);
        break;
    case Dirlist:
        dirlist_next(in);
        break;
    case File_Send:
        put_file_next(in);
        break;
    case File_Receive:
        get_file_next(in);
        break;
    case Raw:
        raw_next(in);
        break;
    case Key:
        key_next(in);
        break;
    }
}

bool usblink_put_file(const char *local, const char *remote, usblink_progress_cb callback, void *user_data) {
    mode = File_Send;

    char *dot = local ? strrchr(local, '.') : NULL;
    // TODO (thanks for the reminder, Excale :P) : Filter depending on which model is being emulated
    if (dot && (!strcmp(dot, ".tno") || !strcmp(dot, ".tnc")
             || !strcmp(dot, ".tco") || !strcmp(dot, ".tcc")
             || !strcmp(dot, ".tmo") || !strcmp(dot, ".tmc")
             || !strcmp(dot, ".tco2") || !strcmp(dot, ".tcc2")
             || !strcmp(dot, ".tct2")) ) {
        emuprintf("File is an OS, calling usblink_send_os\n");
        usblink_send_os(local, callback, user_data);
        return 1;
    }

    current_user_data = user_data;
    current_file_callback = callback;

    if (put_file)
        fclose(put_file);

    if (local && local[0] != '\0') {
        put_file = fopen_utf8(local, "rb");
        if (!put_file) {
            gui_perror(local);
            return 0;
        }
        fseek(put_file, 0, SEEK_END);
        put_file_size_orig = put_file_size = ftell(put_file);
        fseek(put_file, 0, SEEK_SET);
    } else {
        put_file = NULL;
        put_file_size_orig = put_file_size = 0;
    }

    put_file_state = SENDING_03;

    /* Send the first packet */
    struct packet *out = &usblink_send_buffer;
    out->src.service = SID_File;
    out->dst.service = put_file_port = BSWAP16(0x4060);
    out->ack = 0;
    out->seqno = next_seqno();
    uint8_t *data = out->data;
    *data++ = File_Put;
    *data++ = 1;
    data += sprintf((char *)data, "%s", remote) + 1;
    *(uint32_t *)data = BSWAP32(put_file_size); data += 4;
    out->data_size = data - out->data;
    usblink_send_packet();
    return 1;
}

void usblink_new_dir(const char *path, usblink_progress_cb callback, void *user_data)
{
    mode = Dir_Create;
    current_file_callback = callback;
    current_user_data = user_data;

    /* Send the first packet */
    struct packet *out = &usblink_send_buffer;
    out->src.service = SID_File;
    out->dst.service = BSWAP16(0x4060);
    out->ack = 0;
    out->seqno = next_seqno();
    uint8_t *data = out->data;
    *data++ = File_New_Folder;
    *data++ = 3;

    unsigned int size = sprintf((char *)data, "%s", path) + 1;
    data += size;
    while(size < 9)
    {
        *data++ = 0;
        ++size;
    }

    out->data_size = data - out->data;
    usblink_send_packet();
}

bool usblink_send_os(const char *filepath, usblink_progress_cb callback, void *user_data) {
    mode = File_Send;
    current_file_callback = callback;
    current_user_data = user_data;

    FILE *f = fopen_utf8(filepath, "rb");
    if (!f)
        return false;
    if (put_file)
        fclose(put_file);
    put_file = f;
    fseek(f, 0, SEEK_END);
    put_file_size_orig = put_file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    put_file_state = SENDING_03 | EXPECT_FF_00;

    /* Send the first packet */
    struct packet *out = &usblink_send_buffer;
    out->src.service = SID_File;
    out->dst.service = put_file_port = BSWAP16(0x4080);
    out->ack = 0;
    out->seqno = next_seqno();
    uint8_t *data = out->data;
    *data++ = File_Put;
    *(uint32_t *)data = BSWAP32(put_file_size); data += 4;
    out->data_size = data - out->data;
    usblink_send_packet();

    return true;
}

void usblink_move(const char *old_path, const char *new_path, usblink_progress_cb callback, void *user_data)
{
    mode = Rename;
    current_file_callback = callback;
    current_user_data = user_data;

    /* Send the first packet */
    struct packet *out = &usblink_send_buffer;
    out->src.service = SID_File;
    out->dst.service = BSWAP16(0x4060);
    out->ack = 0;
    out->seqno = next_seqno();
    uint8_t *data = out->data;
    *data++ = File_Rename;
    *data++ = 1;
    unsigned int size = sprintf((char *)data, "%s", old_path) + 1;
    data += size;
    while(size < 9)
    {
        *data++ = 0;
        ++size;
    }

    size = sprintf((char *)data, "%s", new_path) + 1;
    data += size;
    while(size < 10)
    {
        *data++ = 0;
        ++size;
    }

    out->data_size = data - out->data;
    usblink_send_packet();
}


bool usblink_get_file(const char *path, const char *dest, usblink_progress_cb callback, void *user_data)
{
    mode = File_Receive;
    current_file_callback = callback;
    current_user_data = user_data;

    if (put_file)
        fclose(put_file);
    put_file = fopen_utf8(dest, "wb");
    if(!put_file)
        return false;
    snprintf(get_file_dest, sizeof(get_file_dest), "%s", dest);

    /* Send the first packet */
    struct packet *out = &usblink_send_buffer;
    out->src.service = SID_File;
    out->dst.service = BSWAP16(0x4060);
    out->ack = 0;
    out->seqno = next_seqno();
    uint8_t *data = out->data;
    *data++ = File_Get;
    *data++ = 1;

    unsigned int size = sprintf((char *)data, "%s", path) + 1;
    data += size;
    while(size < 9)
    {
        *data++ = 0;
        ++size;
    }

    out->data_size = data - out->data;
    usblink_send_packet();

    return true;
}

void usblink_delete(const char *path, bool is_dir, usblink_progress_cb callback, void *user_data)
{
    mode = Delete;
    current_file_callback = callback;
    current_user_data = user_data;

    /* Send the first packet */
    struct packet *out = &usblink_send_buffer;
    out->src.service = is_dir ? SID_Dirlist : SID_File;
    out->dst.service = BSWAP16(0x4060);
    out->ack = 0;
    out->seqno = next_seqno();
    uint8_t *data = out->data;
    if(is_dir)
    {
        *data++ = File_Del_Folder;
        *data++ = 3;
    }
    else
    {
        *data++ = File_Del;
        *data++ = 1;
    }

    unsigned int size = sprintf((char *)data, "%s", path) + 1;
    data += size;
    while(size < 9)
    {
        *data++ = 0;
        ++size;
    }

    out->data_size = data - out->data;
    usblink_send_packet();
}

bool usblink_sending, usblink_connected = false;
int usblink_state;

extern void usb_bus_reset_on(void);
extern void usb_bus_reset_off(void);
extern void usb_receive_setup_packet(int endpoint, void *packet);
extern void usb_receive_packet(int endpoint, void *packet, uint32_t size);

// Drop whatever transfer is in flight but keep the link. The full reset below clears
// usblink_connected too, and on the CX II that cannot be undone without another boot, because the
// OS masks the device interrupts once it has finished starting up.
void usblink_abandon_transfer() {
    key_stage = 0;
    raw_stage = 0;
    put_file_state = 0;
    if (put_file) {
        fclose(put_file);
        put_file = NULL;
    }
    current_file_callback = NULL;
    current_raw_callback = NULL;
    current_user_data = NULL;
    usblink_sending = false;
}

void usblink_reset() {
    key_stage = 0;
    raw_stage = 0;
    next_host_service = SID_Raw;
    if (put_file_state) {
        put_file_state = 0;
        fclose(put_file);
        put_file = NULL;
    }
    usblink_connected = false;
    gui_usblink_changed(usblink_connected);
    usblink_state = 0;
    usblink_sending = false;
    usblink_cx2_reset();
}

void usblink_connect() {
    if(usblink_connected)
        return;

    prev_seqno = 0;
    usblink_state = 1;
}

// no easy way to tell when it's ok to turn bus reset off,
// (putting the device into the default state) so do it on a timer :/
void usblink_timer() {
    switch (usblink_state) {
        case 1:
            if(emulate_cx2)
                usb_cx2_bus_reset_on();
            else
                usb_bus_reset_on();

            usblink_state++;
            break;
        case 2: {
            //printf("Sending SET_ADDRESS\n");
            struct usb_setup packet = { 0, 5, 1, 0, 0 };
            if(emulate_cx2)
            {
                usb_cx2_bus_reset_off();
                usb_cx2_receive_setup_packet(&packet);
            }
            else
            {
                usb_bus_reset_off();
                usb_receive_setup_packet(0, &packet);
            }

            usblink_state++;
            break;
        }
    }
}

void usblink_receive(int ep, void *buf, uint32_t size) {
    //printf("usblink_receive(%d,%p,%d)\n", ep, buf, size);
    if (ep == 0) {
        if (usblink_state == 3) {
            //printf("Sent SET_ADDRESS, sending SET_CONFIGURATION\n");
            struct usb_setup packet = { 0, 9, 1, 0, 0 };
            usb_receive_setup_packet(0, &packet);
            usblink_state = 0;
        }
    } else {
        if (size >= 16)
            usblink_received_packet(buf, size);
    }
}

void usblink_complete_send(int ep) {
    if (ep != 0 && usblink_sending) {
        uint32_t size = 16 + usblink_send_buffer.data_size;
        usb_receive_packet(ep, &usblink_send_buffer, size);
        usblink_sending = false;
        //printf("send complete\n");
    }
}

void usblink_start_send() {
    if(emulate_cx2)
    {
        // Wrap it in NNSE
        usblink_cx2_send_navnet((const uint8_t*) &usblink_send_buffer, 16 + packet_fulldatasize(&usblink_send_buffer));
        return;
    }

    int ep;
    usblink_sending = true;
    // If there's already an endpoint waiting for data, just send immediately
    for (ep = 1; ep < 4; ep++) {
        if (usb.epsr & (1 << ep)) {
            usblink_complete_send(ep);
            return;
        }
    }
}
