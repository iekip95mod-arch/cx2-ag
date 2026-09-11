#include <stdio.h>
#include <string.h>

static int fail_write;
static int fail_close;
static size_t written;

static size_t receive_write(const void *bytes, size_t size, size_t count, FILE *stream) {
    if (fail_write && count)
        --count;
    size_t accepted = fwrite(bytes, size, count, stream);
    written += accepted * size;
    return accepted;
}

static int receive_close(FILE *stream) {
    int status = fclose(stream);
    return fail_close ? EOF : status;
}

#define fwrite receive_write
#define fclose receive_close
#include "../usblink.c"
#undef fwrite
#undef fclose

static unsigned checks;
static unsigned failures;
static int completion;
static unsigned completed;
static unsigned raw_completions;
static unsigned raw_bytes;
static uint8_t raw_value;

static void raw_progress(const uint8_t *bytes, uint32_t size, bool failed, void *context) {
    (void)context;
    ++raw_completions;
    raw_bytes = failed ? UINT32_MAX : size;
    raw_value = !failed && size ? bytes[0] : 0;
}

static void check(bool passed, const char *name) {
    ++checks;
    if (!passed) {
        ++failures;
        fprintf(stderr, "FAIL %s\n", name);
    }
}

static void progress(int value, void *context) {
    (void)context;
    completion = value;
    if (value == 100 || value < 0)
        ++completed;
}

static void begin_receive(uint32_t expected) {
    if (put_file)
        fclose(put_file);
    put_file = tmpfile();
    if (!put_file) {
        perror("tmpfile");
        exit(2);
    }
    current_file_callback = progress;
    current_user_data = NULL;
    get_file_dest[0] = 0;
    fail_write = fail_close = 0;
    written = completed = 0;
    completion = -99;
    product = 0x0f0;
    struct packet header = {0};
    header.data_size = 15;
    header.data[0] = File_Put;
    expected = BSWAP32(expected);
    memcpy(header.data + 11, &expected, sizeof(expected));
    get_file_next(&header);
    check(completion == 0, "metadata starts progress");
}

static struct packet contents(unsigned count) {
    struct packet chunk = {0};
    if (count < 254) {
        chunk.data_size = count + 1;
        memset(chunk.data, 'a', count + 1);
        chunk.data[0] = File_Contents;
    } else {
        chunk.data_size = 255;
        chunk.bigdatasize = BSWAP32(count + 1);
        memset(chunk.bigdata, 'b', sizeof(chunk.bigdata));
        chunk.bigdata[0] = File_Contents;
    }
    return chunk;
}

int main(int argc, char **argv) {
    const char *selected = argc > 1 ? argv[1] : "all";
#define RUN(name) (!strcmp(selected, "all") || !strcmp(selected, name))
    if (RUN("sequence")) {
        const uint8_t query[] = {0, 0, 0, 20};
        product = 0x1c0;
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        check(usblink_send_buffer.seqno == 0, "CX II uses the outer NNSE sequence only");
        usblink_abandon_transfer();
        product = 0x0f0;
        prev_seqno = 255;
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        check(usblink_send_buffer.seqno == 1, "legacy packet sequence still wraps to one");
        usblink_abandon_transfer();
    }
    if (RUN("frame")) {
        mode = Raw;
        raw_stage = 1;
        raw_completions = 0;
        current_host_service = SID_Raw;
        current_service = 0x4b45;
        current_raw_callback = raw_progress;
        for (unsigned length = 0; length < 16; ++length) {
            uint8_t *short_header = calloc(length ? length : 1, 1);
            usblink_received_packet(short_header, length);
            free(short_header);
        }
        check(raw_completions == 0 && raw_stage == 1, "short frames cannot advance a raw request");
        struct packet reply = {0};
        reply.src.service = BSWAP16(0x4b45);
        reply.dst.service = BSWAP16(SID_Raw);
        reply.data_size = 2;
        reply.data[0] = 42;
        usblink_received_packet((const uint8_t *)&reply, 17);
        check(raw_completions == 0 && raw_stage == 1, "truncated payload is ignored");
        reply.data_size = 0xff;
        reply.bigdatasize = BSWAP32(1441);
        usblink_received_packet((const uint8_t *)&reply, sizeof reply);
        check(raw_completions == 0 && raw_stage == 1, "oversized frame is ignored");
        usblink_received_packet((const uint8_t *)&reply, 19);
        check(raw_completions == 0 && raw_stage == 1, "truncated large length is ignored");
        reply.bigdatasize = BSWAP32(2);
        usblink_received_packet((const uint8_t *)&reply, 21);
        check(raw_completions == 0 && raw_stage == 1, "truncated large payload is ignored");
        reply.data_size = 2;
        reply.data[0] = 42;
        uint8_t unaligned[19];
        memcpy(unaligned + 1, &reply, 18);
        usblink_received_packet(unaligned + 1, 18);
        usblink_received_packet(NULL, 0);
        check(raw_completions == 1 && raw_bytes == 2 && raw_value == 42,
              "complete unaligned frame retains its response");
        raw_stage = 1;
        current_raw_callback = raw_progress;
        reply.data_size = 0;
        usblink_received_packet((const uint8_t *)&reply, 16);
        usblink_received_packet(NULL, 0);
        check(raw_completions == 2 && raw_bytes == 0, "complete empty frame is accepted");
        raw_stage = 1;
        current_raw_callback = raw_progress;
        reply.data_size = 0xff;
        reply.bigdatasize = BSWAP32(257);
        reply.bigdata[0] = 84;
        usblink_received_packet((const uint8_t *)&reply, 277);
        usblink_received_packet(NULL, 0);
        check(raw_completions == 3 && raw_bytes == 257 && raw_value == 84,
              "complete large frame retains its response");
        raw_completions = 0;
    }
    if (RUN("empty")) {
        begin_receive(0);
        struct packet chunk = contents(0);
        get_file_next(&chunk);
        check(completion == 100 && completed == 1 && !put_file, "empty file completes once");
        check(written == 0, "empty file writes no bytes");
    }
    if (RUN("overflow")) {
        begin_receive(1);
        struct packet chunk = contents(1439);
        get_file_next(&chunk);
        check(completion == -1 && completed == 1 && !put_file, "oversized contents rejected");
        check(written == 0, "oversized contents never written");
    }
    if (RUN("length")) {
        begin_receive(2000);
        struct packet chunk = contents(1439);
        chunk.bigdatasize = BSWAP32(1441);
        get_file_next(&chunk);
        check(completion == -1 && !put_file && written == 0, "invalid extended packet length rejected");
    }
    if (RUN("late")) {
        begin_receive(1);
        struct packet chunk = contents(1);
        get_file_next(&chunk);
        get_file_next(&chunk);
        check(completion == 100 && completed == 1 && written == 1, "late contents ignored");
    }
    if (RUN("write")) {
        begin_receive(3);
        fail_write = 1;
        struct packet chunk = contents(3);
        get_file_next(&chunk);
        check(completion == -1 && completed == 1 && !put_file, "short host write fails transfer");
        check(usblink_send_buffer.data[1] != 0, "write failure sends failure status");
    }
    if (RUN("close")) {
        begin_receive(3);
        fail_close = 1;
        struct packet chunk = contents(3);
        get_file_next(&chunk);
        check(completion == -1 && completed == 1 && !put_file, "host flush failure fails transfer");
        check(usblink_send_buffer.data[1] != 0, "flush failure sends failure status");
    }
    if (RUN("progress")) {
        begin_receive(UINT32_MAX);
        put_file_size = 0x80000000;
        struct packet chunk = contents(253);
        get_file_next(&chunk);
        check(completion == 49 && completed == 0, "large file progress does not overflow");
    }
    if (RUN("normal")) {
        begin_receive(300);
        struct packet chunk = contents(253);
        get_file_next(&chunk);
        check(completed == 0 && put_file && written == 253, "full packet keeps transfer open");
        chunk = contents(47);
        get_file_next(&chunk);
        check(completion == 100 && completed == 1 && !put_file && written == 300, "multiple packets complete exact size");
        check(usblink_send_buffer.data[1] == 0, "successful transfer acknowledged");
        begin_receive(1439);
        chunk = contents(1439);
        get_file_next(&chunk);
        check(completion == 100 && completed == 1 && !put_file && written == 1439, "extended packet completes exact size");
    }
    if (RUN("short")) {
        begin_receive(4);
        struct packet chunk = contents(3);
        get_file_next(&chunk);
        check(completion == -1 && completed == 1 && !put_file, "truncated final packet fails transfer");
        check(usblink_send_buffer.data[1] != 0, "truncated transfer sends failure status");
    }
    if (RUN("raw")) {
        const uint8_t query[] = {0, 0, 0, 20};
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        uint16_t host = usblink_send_buffer.src.service;
        struct packet reply = {0};
        reply.src.service = BSWAP16(0x4b45);
        reply.dst.service = host;
        reply.data_size = 1;
        reply.data[0] = 0x6b;
        raw_next(&reply);
        check(usblink_send_buffer.src.service == BSWAP16(0x40de) &&
              usblink_send_buffer.dst.service == BSWAP16(0x4b45), "raw disconnect targets the original service");
        check(raw_completions == 0, "raw reply waits for disconnect acknowledgement");
        raw_next(NULL);
        check(raw_completions == 1 && raw_bytes == 1 && raw_value == 0x6b, "raw reply survives disconnect");
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        check(usblink_send_buffer.src.service != host, "new raw exchange uses a fresh host conversation");
        raw_next(&reply);
        check(raw_completions == 1, "late raw reply cannot finish the next conversation");
        usblink_abandon_transfer();
        raw_next(NULL);
        check(raw_completions == 1, "abandoned raw exchange ignores late acknowledgements");
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        reply.dst.service = usblink_send_buffer.src.service;
        reply.data_size = 0xff;
        reply.bigdatasize = BSWAP32(1441);
        raw_next(&reply);
        raw_next(NULL);
        check(raw_completions == 2 && raw_bytes == UINT32_MAX, "oversized raw response fails without copying past the packet");
        next_host_service = UINT16_MAX;
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        check(usblink_send_buffer.src.service == BSWAP16(UINT16_MAX), "last host conversation identifier is usable");
        reply.dst.service = usblink_send_buffer.src.service;
        reply.data_size = 0;
        raw_next(&reply);
        raw_next(NULL);
        check(raw_completions == 3 && raw_bytes == 0, "empty raw response is preserved");
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        check(usblink_send_buffer.src.service == BSWAP16(SID_Raw), "host conversation wrap avoids reserved file services");
        reply.dst.service = usblink_send_buffer.src.service;
        reply.src.service = BSWAP16(0x00d3);
        raw_next(&reply);
        raw_next(NULL);
        check(raw_completions == 4 && raw_bytes == UINT32_MAX, "raw service refusal completes once");
        usblink_raw(0x4b45, query, sizeof query, raw_progress, NULL);
        usblink_reset();
        raw_next(NULL);
        check(raw_completions == 4, "reset discards the pending raw conversation");
    }
    if (RUN("key")) {
        completed = 0;
        completion = -99;
        usblink_send_key(0x0d1000, progress, NULL);
        uint16_t host = usblink_send_buffer.src.service;
        const uint8_t initialize[] = {1, 0, 0, 0x80};
        check(usblink_send_buffer.dst.service == BSWAP16(0x4042) &&
              usblink_send_buffer.data_size == 4 &&
              !memcmp(usblink_send_buffer.data, initialize, sizeof initialize), "OS key initializes service");
        key_next(NULL);
        uint8_t enter[26] = {0};
        enter[4] = 8;
        enter[5] = 2;
        enter[6] = 13;
        enter[8] = 16;
        check(usblink_send_buffer.data_size == sizeof enter &&
              !memcmp(usblink_send_buffer.data, enter, sizeof enter), "OS key sends exact Enter packet");
        check(completed == 0, "key send does not claim completion before disconnect");
        key_next(NULL);
        check(usblink_send_buffer.dst.service == BSWAP16(0x4042) &&
              usblink_send_buffer.data_size == 2 &&
              usblink_send_buffer.data[0] == (BSWAP16(host) >> 8) &&
              usblink_send_buffer.data[1] == (BSWAP16(host) & 0xff), "OS key closes its own conversation");
        key_next(NULL);
        key_next(NULL);
        check(completed == 1 && completion == 100, "OS key completes once after disconnect acknowledgement");
        usblink_send_key(0x1000000, progress, NULL);
        check(completed == 2 && completion == -1 && key_stage == 0, "invalid OS key is refused");
        usblink_send_key(0x0d1000, progress, NULL);
        struct packet refused = {0};
        refused.src.service = BSWAP16(0x00d3);
        refused.dst.service = usblink_send_buffer.src.service;
        key_next(&refused);
        key_next(NULL);
        check(completed == 3 && completion == -1 && key_stage == 0, "service refusal cannot complete as success");
        usblink_send_key(0x0d1000, progress, NULL);
        usblink_abandon_transfer();
        key_next(NULL);
        check(completed == 3 && key_stage == 0, "abandoned key does not advance on a late acknowledgement");
        usblink_send_key(0x0d1000, progress, NULL);
        usblink_reset();
        key_next(NULL);
        check(completed == 3 && key_stage == 0, "reset discards the pending OS key");
    }
    if (put_file) {
        fclose(put_file);
        put_file = NULL;
    }
    printf("usblinktest: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
