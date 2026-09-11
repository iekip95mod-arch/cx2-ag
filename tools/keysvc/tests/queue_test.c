#include <assert.h>
#include <time.h>

static int queue_sleep(const struct timespec *, struct timespec *);
#define nanosleep queue_sleep
#define main nsptool_main
#include "../../nsptool/nsptool.c"
#undef main
#undef nanosleep
#define main keysvc_main
#include "../keysvc.c"
#undef main

static unsigned exchanges, sleeps, event_count;
static const void *request_bytes;
static size_t request_size;
static void *response_bytes;
static size_t response_size;
static unsigned short event_codes[1024], event_modifiers[1024];
static BOOL event_releases[1024];

unsigned nl_hwsubtype(void)
{
	return 2;
}

void send_key_event(struct s_ns_event *event, unsigned short code, BOOL up, BOOL repeat)
{
	assert(response_size >= 2 && repeat);
	assert(event_count < 1024);
	event_codes[event_count] = code;
	event_modifiers[event_count] = event->modifiers;
	event_releases[event_count++] = up;
}

int16_t TI_NN_Read(nn_ch_t channel, unsigned timeout, void *bytes, unsigned capacity, uint32_t received)
{
	(void)channel;
	(void)timeout;
	(void)received;
	assert(request_size <= capacity);
	memcpy(bytes, request_bytes, request_size);
	got = request_size;
	return 0;
}

int16_t TI_NN_Write(nn_ch_t channel, const void *bytes, unsigned length)
{
	(void)channel;
	memcpy(response_bytes, bytes, length);
	response_size = length;
	return 0;
}

const char *nspire_strerror(int error)
{
	(void)error;
	return "queue test transport error";
}

int nspire_service_exchange(nspire_handle_t *handle, uint16_t sid, const void *request,
			   size_t length, void *reply, size_t capacity, size_t *received)
{
	(void)handle;
	assert(sid == KEYSVC_SID && capacity >= 4);
	request_bytes = request;
	request_size = length;
	response_bytes = reply;
	response_size = 0;
	exchanges++;
	serve(NULL, NULL);
	*received = response_size;
	return 0;
}

static int queue_sleep(const struct timespec *duration, struct timespec *remaining)
{
	(void)remaining;
	assert(duration->tv_sec == 0 && duration->tv_nsec == 100000000);
	assert(held.count == 2);
	assert(held.keys[0].code == 0xB001 && held.keys[1].code == 0xB102);
	sleeps++;
	return 0;
}

int main(void)
{
	char *chord[] = { "+ctrl+a+b", "wait:100", "-b", "-a" };
	assert(cmd_key(NULL, 4, chord) == 0);
	assert(exchanges == 3 && sleeps == 1 && held.count == 0 && event_count == 4);
	assert(event_codes[0] == 0xB001 && event_codes[1] == 0xB102);
	assert(event_codes[2] == 0xB102 && event_codes[3] == 0xB001);
	assert(!event_releases[0] && !event_releases[1] && event_releases[2] && event_releases[3]);
	assert(event_modifiers[0] == 4 && event_modifiers[1] == 4);
	assert(event_modifiers[2] == 4 && event_modifiers[3] == 0);
	char *hold[] = { "+ctrl" }, *release[] = { "-ctrl" };
	assert(cmd_key(NULL, 1, hold) == 0 && held.count == 1);
	assert(cmd_type(NULL, "A") == 0 && held.count == 1);
	assert(event_codes[5] == 0xB001 && event_modifiers[5] == 7);
	assert(event_modifiers[6] == 4);
	assert(cmd_key(NULL, 1, release) == 0 && held.count == 0);
	assert(event_modifiers[7] == 0);
	char *many[130];
	for (unsigned i = 0; i < 130; i++)
		many[i] = "down";
	unsigned before = exchanges;
	assert(cmd_key(NULL, 130, many) == 0);
	assert(exchanges == before + 3 && event_count == 268 && held.count == 0);
	before = exchanges;
	assert(cmd_restart(NULL) == 0 && exchanges == before + 1);
	assert(keysvc_test_restart_register == 0x80);
	puts("keysvc queue: host parser, waits, batches, service translation and releases passed");
	return 0;
}
