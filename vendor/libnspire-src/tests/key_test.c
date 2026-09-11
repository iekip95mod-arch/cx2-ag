#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "api/nspire.h"
#include "cx2.h"
#include "data.h"
#include "packet.h"

static nspire_handle_t *active;
static unsigned sent[3], received, phase;
static int write_fault[3], ack_fault[3];
static uint8_t expected_key[26];

int packet_prepare_cx2(nspire_handle_t *handle)
{
	assert(handle == active && handle->is_cx2);
	return 0;
}

static int send_packet(void *bytes, int length)
{
	const uint8_t *wire = bytes;
	assert(active->connected && active->device_sid == 0x4042);
	assert(length >= 16 && wire[8] == 0x40 && wire[9] == 0x42);
	assert(wire[12] == length - 16);
	if (wire[4] == 0x40 && wire[5] == 0xde) {
		phase = 2;
		const uint8_t close[] = { 0x80, 0x00 };
		assert(length == 18 && !memcmp(wire + 16, close, sizeof close));
	} else {
		assert(wire[4] == 0x80 && wire[5] == 0x00 && !sent[2]);
		if (length == 20) {
			phase = 0;
			const uint8_t initialize[] = { 1, 0, 0, 0x80 };
			assert(!sent[1] && !memcmp(wire + 16, initialize, sizeof initialize));
		} else {
			phase = 1;
			assert(sent[0] == 1 && !write_fault[0] && !ack_fault[0]);
			assert(length == 42 && !memcmp(wire + 16, expected_key, sizeof expected_key));
		}
	}
	assert(++sent[phase] == 1);
	return write_fault[phase];
}

int packet_send_cx2(nspire_handle_t *handle, char *bytes, int length)
{
	assert(handle == active && handle->is_cx2);
	int status = send_packet(bytes, length);
	return status ? status : ack_fault[phase];
}

int usb_write(usb_device_t *device, void *bytes, int length)
{
	assert(device == &active->device && !active->is_cx2);
	return send_packet(bytes, length);
}

int packet_recv(nspire_handle_t *handle, struct packet *packet)
{
	assert(handle == active && !handle->is_cx2 && sent[phase] && !write_fault[phase]);
	++received;
	if (ack_fault[phase]) return ack_fault[phase];
	memset(packet, 0, sizeof *packet);
	packet->src_sid = 0xff;
	packet->dst_sid = handle->host_sid;
	return 0;
}

int packet_recv_cx2(nspire_handle_t *handle, char *bytes, int length)
{
	(void)handle; (void)bytes; (void)length;
	assert(!"standard key send must not wait for a CX2 application reply");
	return -1;
}

int usb_read(usb_device_t *device, void *bytes, int length)
{
	(void)device; (void)bytes; (void)length;
	assert(!"legacy test supplies transport ACK packets without USB");
	return -1;
}

static void reset(nspire_handle_t *handle, int cx2)
{
	memset(handle, 0, sizeof *handle);
	handle->host_sid = 0x8000;
	handle->is_cx2 = cx2;
	active = handle;
	memset(sent, 0, sizeof sent);
	memset(write_fault, 0, sizeof write_fault);
	memset(ack_fault, 0, sizeof ack_fault);
	received = 0;
}

static void expect_completion(nspire_handle_t *handle, int status)
{
	int initial = write_fault[0] ? write_fault[0] : ack_fault[0];
	int key = write_fault[1] ? write_fault[1] : ack_fault[1];
	int close = write_fault[2] ? write_fault[2] : ack_fault[2];
	assert(status == (initial ? initial : key ? key : close));
	assert(sent[0] == 1 && sent[1] == (initial ? 0 : 1) && sent[2] == 1);
	assert(handle->connected == (close ? 1 : 0));
	assert(handle->host_sid == (close ? 0x8000 : 0x8001));
	unsigned acknowledgments = 0;
	for (unsigned i = 0; i < 3; ++i)
		acknowledgments += sent[i] && !write_fault[i];
	assert(received == (handle->is_cx2 ? 0 : acknowledgments));
}

int main(void)
{
	const struct { uint32_t key; uint8_t high, middle, low; } keys[] = {
		{ 0x1b9600, 0x1b, 0x96, 0 }, { 0x123456, 0x12, 0x34, 0x56 },
		{ 0xffffff, 0xff, 0xff, 0xff }, { 0, 0, 0, 0 },
		{ 0x00005a, 0, 0, 0x5a }, { 0x340000, 0x34, 0, 0 }, { 0x005600, 0, 0x56, 0 }
	};
	unsigned cases = 0;
	for (int cx2 = 0; cx2 <= 1; ++cx2) {
		nspire_handle_t handle;
		for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; ++i) {
			reset(&handle, cx2);
			memset(expected_key, 0, sizeof expected_key);
			expected_key[4] = 8; expected_key[5] = 2;
			expected_key[6] = keys[i].high; expected_key[8] = keys[i].middle; expected_key[24] = keys[i].low;
			expect_completion(&handle, nspire_send_key(&handle, keys[i].key));
			++cases;
		}
		for (unsigned failed_phase = 0; failed_phase < 3; ++failed_phase) {
			for (unsigned ack = 0; ack <= 1; ++ack) {
				reset(&handle, cx2);
				if (ack) ack_fault[failed_phase] = -NSPIRE_ERR_TIMEOUT;
				else write_fault[failed_phase] = -NSPIRE_ERR_DISCONNECTED;
				expect_completion(&handle, nspire_send_key(&handle, 0x005600));
				++cases;
			}
		}
		for (unsigned failed_phase = 0; failed_phase < 2; ++failed_phase) {
			reset(&handle, cx2);
			ack_fault[failed_phase] = -NSPIRE_ERR_TIMEOUT;
			write_fault[2] = -NSPIRE_ERR_DISCONNECTED;
			expect_completion(&handle, nspire_send_key(&handle, 0x005600));
			++cases;
		}
		reset(&handle, cx2);
		handle.connected = 1; handle.device_sid = 0x7777;
		assert(nspire_send_key(&handle, 0x005600) == -NSPIRE_ERR_BUSY);
		assert(handle.connected && handle.device_sid == 0x7777 && handle.host_sid == 0x8000);
		assert(!sent[0] && !sent[1] && !sent[2] && !received);
		assert(nspire_send_key(&handle, 0x1000000) == -NSPIRE_ERR_INVALID);
		assert(handle.connected && handle.device_sid == 0x7777 && !sent[0] && !sent[2]);
		assert(nspire_send_key(NULL, UINT32_MAX) == -NSPIRE_ERR_INVALID);
		cases += 3;
	}
	printf("key service: %u packet/ACK/failure contracts passed across classic and CX2\n", cases);
	return 0;
}
