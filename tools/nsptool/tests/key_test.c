#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static int test_printf(const char *format, ...);
static int test_fprintf(FILE *stream, const char *format, ...);
static int test_nanosleep(const struct timespec *duration, struct timespec *remaining);

#define printf test_printf
#define fprintf test_fprintf
#define nanosleep test_nanosleep
#define nspire_service_exchange test_exchange
#define nspire_strerror test_strerror
#define main nsptool_program_main
#include "../nsptool.c"
#undef main
#undef nspire_strerror
#undef nspire_service_exchange
#undef nanosleep
#undef fprintf
#undef printf

struct exchange_call {
	struct keyrec recs[KEYSVC_MAX_RECORDS];
	size_t count;
	unsigned waits_before;
};

static struct exchange_call calls[128];
static unsigned call_count, wait_count, waited_ms;
static unsigned fail_call, second_fail_call, partial_call, malformed_call, signal_call;
static unsigned extra_reply_call;
static unsigned query_version, query_capacity, query_length;
static int wait_signal, wait_eintr;
static int exchange_error;
static char last_error[512], last_output[512];

static int test_printf(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	int length = vsnprintf(last_output, sizeof last_output, format, arguments);
	va_end(arguments);
	return length;
}

static int test_fprintf(FILE *stream, const char *format, ...)
{
	(void)stream;
	va_list arguments;
	va_start(arguments, format);
	size_t used = strlen(last_error);
	int length = vsnprintf(last_error + used, sizeof last_error - used, format, arguments);
	va_end(arguments);
	return length;
}

const char *test_strerror(int error)
{
	if (error == -NSPIRE_ERR_TIMEOUT)
		return "Timeout";
	if (error == -NSPIRE_ERR_DISCONNECTED)
		return "USB device disconnected";
	return "mock transport failure";
}

int test_exchange(nspire_handle_t *handle, uint16_t service_id, const void *request,
		  size_t request_len, void *reply, size_t reply_max, size_t *reply_len)
{
	(void)handle;
	assert(service_id == KEYSVC_SID);
	assert(request_len % sizeof(struct keyrec) == 0);
	assert(request_len <= sizeof calls[0].recs);
	assert(call_count < sizeof calls / sizeof calls[0]);
	struct exchange_call *call = &calls[call_count++];
	memcpy(call->recs, request, request_len);
	call->count = request_len / sizeof(struct keyrec);
	call->waits_before = wait_count;
	if (signal_call == call_count)
		raise(SIGINT);
	if (fail_call == call_count || second_fail_call == call_count)
		return exchange_error;
	assert(reply_max >= 4);
	uint8_t *answer = reply;
	answer[0] = 'k';
	answer[1] = partial_call == call_count ? 0 : call->count;
	*reply_len = 2;
	if (malformed_call == call_count) {
		answer[0] = '?';
		*reply_len = 1;
	}
	if (extra_reply_call == call_count) {
		answer[2] = 0;
		*reply_len = 3;
	}
	if (call->count == 1 && call->recs[0].action == ACT_QUERY) {
		assert(call->recs[0].code_lo == 0 && call->recs[0].code_hi == 0);
		assert(call->recs[0].modifiers == 0);
		answer[2] = query_version;
		answer[3] = query_capacity;
		*reply_len = query_length;
	}
	return 0;
}

static int test_nanosleep(const struct timespec *duration, struct timespec *remaining)
{
	assert(duration->tv_sec >= 0 && duration->tv_nsec >= 0 && duration->tv_nsec < 1000000000);
	unsigned milliseconds = duration->tv_sec * 1000 + duration->tv_nsec / 1000000;
	wait_count++;
	if (wait_signal) {
		raise(wait_signal);
		errno = EINTR;
		return -1;
	}
	if (wait_eintr) {
		wait_eintr = 0;
		waited_ms += milliseconds / 2;
		milliseconds -= milliseconds / 2;
		*remaining = (struct timespec){ milliseconds / 1000, (milliseconds % 1000) * 1000000L };
		errno = EINTR;
		return -1;
	}
	waited_ms += milliseconds;
	return 0;
}

static void reset_transport(void)
{
	memset(calls, 0, sizeof calls);
	call_count = wait_count = waited_ms = 0;
	fail_call = second_fail_call = partial_call = malformed_call = signal_call = 0;
	extra_reply_call = 0;
	query_version = KEYSVC_VERSION;
	query_capacity = KEYSVC_MAX_HELD;
	query_length = 4;
	wait_signal = wait_eintr = 0;
	exchange_error = -NSPIRE_ERR_TIMEOUT;
	last_error[0] = '\0';
	last_output[0] = '\0';
}

static void expect_record(unsigned call, unsigned record, const char *name, unsigned modifiers, unsigned action)
{
	assert(call < call_count && record < calls[call].count);
	const struct keyrec *rec = &calls[call].recs[record];
	assert(rec->action == action);
	assert(rec->modifiers == modifiers);
	unsigned index = rec->code_lo | rec->code_hi << 8;
	if (name) {
		assert(index < sizeof keydefs / sizeof keydefs[0]);
		assert(strcmp(keydefs[index].name, name) == 0);
	} else {
		assert(index == 0);
	}
}

static void check_waits(void)
{
	char *args[] = { "down", "wait:250", "down", "enter" };
	reset_transport();
	assert(cmd_key(NULL, 4, args) == 0);
	assert(call_count == 3 && wait_count == 1 && waited_ms == 250);
	assert(calls[1].count == 1 && calls[1].waits_before == 0);
	assert(calls[2].count == 2 && calls[2].waits_before == 1);
	expect_record(1, 0, "down", 0, ACT_KEY_TAP);
	expect_record(2, 0, "down", 0, ACT_KEY_TAP);
	expect_record(2, 1, "enter", 0, ACT_KEY_TAP);
	reset_transport();
	wait_eintr = 1;
	assert(cmd_key(NULL, 4, args) == 0);
	assert(wait_count == 2 && waited_ms == 250);
	char *maximum[] = { "wait:0", "a", "wait:300000" };
	reset_transport();
	assert(cmd_key(NULL, 3, maximum) == 0);
	assert(wait_count == 2 && waited_ms == MAX_KEY_WAIT_MS);
}

static void check_chords(void)
{
	char *args[] = { "a+b", "ctrl+shift+a", "+a+b", "wait:100", "-a+b", "+ctrl", "a", "-ctrl", "release-all" };
	reset_transport();
	assert(cmd_key(NULL, 9, args) == 0);
	assert(call_count == 3 && waited_ms == 100);
	assert(calls[1].count == 7 && calls[2].count == 6);
	expect_record(1, 0, "a", 0, ACT_KEY_PRESS);
	expect_record(1, 1, "b", 0, ACT_KEY_PRESS);
	expect_record(1, 2, "b", 0, ACT_KEY_RELEASE);
	expect_record(1, 3, "a", 0, ACT_KEY_RELEASE);
	expect_record(1, 4, "a", MOD_CTRL | MOD_SHIFT, ACT_KEY_TAP);
	expect_record(1, 5, "a", 0, ACT_KEY_PRESS);
	expect_record(1, 6, "b", 0, ACT_KEY_PRESS);
	expect_record(2, 0, "b", 0, ACT_KEY_RELEASE);
	expect_record(2, 1, "a", 0, ACT_KEY_RELEASE);
	expect_record(2, 2, "ctrl", 0, ACT_KEY_PRESS);
	expect_record(2, 3, "a", 0, ACT_KEY_TAP);
	expect_record(2, 4, "ctrl", 0, ACT_KEY_RELEASE);
	expect_record(2, 5, NULL, 0, ACT_RELEASE_ALL);

	char *modifiers[] = { "ctrl+shift", "shift+ctrl+a+b", "+ctrl+shift+a+b", "-ctrl+shift+a+b" };
	reset_transport();
	assert(cmd_key(NULL, 4, modifiers) == 0);
	assert(call_count == 2 && calls[1].count == 12);
	expect_record(1, 0, "ctrl", 0, ACT_KEY_PRESS);
	expect_record(1, 1, "shift", 0, ACT_KEY_PRESS);
	expect_record(1, 2, "shift", 0, ACT_KEY_RELEASE);
	expect_record(1, 3, "ctrl", 0, ACT_KEY_RELEASE);
	expect_record(1, 4, "a", 7, ACT_KEY_PRESS);
	expect_record(1, 5, "b", 7, ACT_KEY_PRESS);
	expect_record(1, 6, "b", 7, ACT_KEY_RELEASE);
	expect_record(1, 7, "a", 7, ACT_KEY_RELEASE);
	expect_record(1, 8, "a", 7, ACT_KEY_PRESS);
	expect_record(1, 9, "b", 7, ACT_KEY_PRESS);
	expect_record(1, 10, "b", 7, ACT_KEY_RELEASE);
	expect_record(1, 11, "a", 7, ACT_KEY_RELEASE);
}

static void check_validation(void)
{
	char *invalid[] = {
		"", "+", "-", "a+", "+a++b", "a+a", "home+on", "ctrl+ctrl+a",
		"shift+shift", "a+ctrl", "a+unknown", "wait:", "wait:-1", "wait:+1",
		"wait:1.5", "wait:300001", "wait:99999999999999999999999999999",
		"a+b+c+d+e+f+g+h", "ctrl+a+b+c+d+e+f+g+h", "release-all+a",
		"q+u", "r+v", "s+w"
	};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		char *args[] = { "down", invalid[i] };
		reset_transport();
		assert(cmd_key(NULL, 2, args) == 2);
		assert(call_count == 0 && wait_count == 0);
	}
	char *too_long_wait[] = { "down", "wait:300000", "wait:1" };
	reset_transport();
	assert(cmd_key(NULL, 3, too_long_wait) == 2 && call_count == 0);
	char *duplicate_hold[] = { "+a", "wait:1", "+a" };
	assert(cmd_key(NULL, 3, duplicate_hold) == 2 && call_count == 0);
	char *held_tap[] = { "+home", "on" };
	assert(cmd_key(NULL, 2, held_tap) == 2 && call_count == 0);
	char *held_limit[] = { "+ctrl", "+a+b+c+d+e+f", "g" };
	assert(cmd_key(NULL, 3, held_limit) == 2 && call_count == 0);
	char long_name[129];
	memset(long_name, 'a', sizeof long_name - 1);
	long_name[sizeof long_name - 1] = '\0';
	char *overlong[] = { long_name };
	assert(cmd_key(NULL, 1, overlong) == 2 && call_count == 0);
	assert(cmd_key(NULL, 0, NULL) == 2 && call_count == 0);
	char *only_wait[] = { "wait:1" };
	assert(cmd_key(NULL, 1, only_wait) == 2 && call_count == 0);
}

static void check_frozen_codes(void)
{
	char *plain[] = { "+q", "wait:1", "u" };
	reset_transport();
	assert(cmd_key(NULL, 3, plain) == 2 && call_count == 0);
	char *frozen_plain[] = { "+u", "+shift", "wait:1", "q" };
	assert(cmd_key(NULL, 4, frozen_plain) == 2 && call_count == 0);
	char *frozen_shifted[] = { "+shift", "+u", "-shift", "wait:1", "q", "-u" };
	assert(cmd_key(NULL, 6, frozen_shifted) == 0 && call_count == 3);
	char *controlled[] = { "+ctrl", "q+u", "-ctrl", "shift+q+u" };
	reset_transport();
	assert(cmd_key(NULL, 4, controlled) == 0 && call_count == 2);
	char *implicit_ctrl[] = { "+ctrl+a", "q+u", "-ctrl+a" };
	reset_transport();
	assert(cmd_key(NULL, 3, implicit_ctrl) == 0 && call_count == 2);
	char *aliases[] = { "+home", "wait:1", "-on", "home" };
	reset_transport();
	assert(cmd_key(NULL, 4, aliases) == 0 && call_count == 3);
}

static void check_known_releases(void)
{
	char *after_tap[] = { "down", "wait:1", "-down" };
	reset_transport();
	assert(cmd_key(NULL, 3, after_tap) == 2 && call_count == 0 && wait_count == 0);
	char *twice[] = { "+a", "wait:1", "-a", "-a" };
	assert(cmd_key(NULL, 4, twice) == 2 && call_count == 0 && wait_count == 0);
	char *after_all[] = { "release-all", "wait:1", "-a" };
	assert(cmd_key(NULL, 3, after_all) == 2 && call_count == 0 && wait_count == 0);
	char *alias_twice[] = { "+home", "-on", "wait:1", "-home" };
	assert(cmd_key(NULL, 4, alias_twice) == 2 && call_count == 0 && wait_count == 0);
	char *alias_tap[] = { "on", "wait:1", "-home" };
	assert(cmd_key(NULL, 3, alias_tap) == 2 && call_count == 0 && wait_count == 0);
	char *initial[] = { "-a", "+a", "wait:1", "-a" };
	assert(cmd_key(NULL, 4, initial) == 0 && call_count == 3);
	char *pressed_after_all[] = { "release-all", "+a", "wait:1", "-a" };
	reset_transport();
	assert(cmd_key(NULL, 4, pressed_after_all) == 0 && call_count == 3);
}

static void check_batches(void)
{
	char *args[MAX_KEY_RECORDS + 1];
	for (unsigned i = 0; i < MAX_KEY_RECORDS + 1; ++i)
		args[i] = "a";
	args[127] = "a+b";
	reset_transport();
	assert(cmd_key(NULL, 129, args) == 0);
	assert(call_count == 3 && calls[1].count == 127 && calls[2].count == 5);
	expect_record(2, 0, "a", 0, ACT_KEY_PRESS);
	expect_record(2, 1, "b", 0, ACT_KEY_PRESS);
	expect_record(2, 2, "b", 0, ACT_KEY_RELEASE);
	expect_record(2, 3, "a", 0, ACT_KEY_RELEASE);
	args[127] = "a";
	reset_transport();
	assert(cmd_key(NULL, MAX_KEY_RECORDS, args) == 0);
	assert(call_count == 33);
	for (unsigned i = 1; i < call_count; ++i)
		assert(calls[i].count == KEYSVC_MAX_RECORDS);
	reset_transport();
	assert(cmd_key(NULL, MAX_KEY_RECORDS + 1, args) == 2 && call_count == 0);
}

static void check_versions(void)
{
	char *args[] = { "down" };
	const unsigned malformed_lengths[] = { 0, 1, 2, 3, 5, 16 };
	for (unsigned i = 0; i < sizeof malformed_lengths / sizeof malformed_lengths[0]; ++i) {
		reset_transport();
		query_length = malformed_lengths[i];
		assert(cmd_key(NULL, 1, args) == 1 && call_count == 1);
		expect_record(0, 0, NULL, 0, ACT_QUERY);
		assert(strstr(last_error, "unrecognized reply") && strstr(last_error, "unknown"));
		assert(!strstr(last_error, "Install") && !strstr(last_error, "restart"));
	}
	reset_transport();
	malformed_call = 1;
	assert(cmd_key(NULL, 1, args) == 1 && call_count == 1);
	assert(strstr(last_error, "unrecognized reply") && !strstr(last_error, "Install"));
	reset_transport();
	partial_call = 1;
	assert(cmd_key(NULL, 1, args) == 1 && call_count == 1);
	assert(strstr(last_error, "unrecognized reply") && !strstr(last_error, "Install"));
	reset_transport();
	query_version = 1;
	assert(cmd_key(NULL, 1, args) == 1 && call_count == 1);
	assert(strstr(last_error, "reports version 1") && strstr(last_error, "Required: version 2"));
	assert(strstr(last_error, "Install") && strstr(last_error, "restart"));
	reset_transport();
	query_capacity = 6;
	assert(cmd_key(NULL, 1, args) == 1 && call_count == 1);
	assert(strstr(last_error, "capacity 6") && strstr(last_error, "capacity at least 7"));
	assert(strstr(last_error, "Install") && strstr(last_error, "restart"));
	reset_transport();
	fail_call = 1;
	assert(cmd_key(NULL, 1, args) == 1 && call_count == 1);
	assert(strstr(last_error, "keysvc version query: Timeout"));
	assert(!strstr(last_error, "Install") && !strstr(last_error, "restart"));
	reset_transport();
	fail_call = 1;
	exchange_error = -NSPIRE_ERR_DISCONNECTED;
	assert(cmd_key(NULL, 1, args) == 1 && call_count == 1);
	assert(strstr(last_error, "keysvc version query: USB device disconnected"));
	assert(!strstr(last_error, "Install") && !strstr(last_error, "restart"));
}

static void expect_query_only(void)
{
	assert(call_count == 1 && calls[0].count == 1 && !wait_count);
	expect_record(0, 0, NULL, 0, ACT_QUERY);
	assert(!last_output[0]);
	assert(!strstr(last_error, "key state may be unknown"));
	assert(strstr(last_error, "No keys from this request were dispatched."));
}

static void check_predispatch(void)
{
	char *args[] = { "right", "wait:1", "enter" };
	for (unsigned typed = 0; typed < 2; ++typed) {
		for (unsigned failure = 0; failure < 6; ++failure) {
			reset_transport();
			switch (failure) {
			case 0: fail_call = 1; break;
			case 1: fail_call = 1; exchange_error = -NSPIRE_ERR_DISCONNECTED; break;
			case 2: query_length = 2; break;
			case 3: malformed_call = 1; break;
			case 4: query_version = 1; break;
			case 5: query_capacity = KEYSVC_MAX_HELD - 1; break;
			}
			assert((typed ? cmd_type(NULL, "Ab2") : cmd_key(NULL, 3, args)) == 1);
			if (failure == 0)
				assert(strstr(last_error, "keysvc version query: Timeout"));
			expect_query_only();
		}
	}
	reset_transport();
	assert(cmd_key(NULL, 3, args) == 0 && call_count == 3 && wait_count == 1);
	assert(!last_error[0]);
	expect_record(1, 0, "right", 0, ACT_KEY_TAP);
	expect_record(2, 0, "enter", 0, ACT_KEY_TAP);
}

static void check_status(void)
{
	reset_transport();
	query_capacity = 9;
	assert(require_keysvc(NULL, 1) == 0 && call_count == 1);
	expect_record(0, 0, NULL, 0, ACT_QUERY);
	assert(strstr(last_output, "keysvc version 2\nheld capacity 9\ncompatible yes\n"));
	assert(!last_error[0] && !wait_count);
	reset_transport();
	fail_call = 1;
	assert(require_keysvc(NULL, 1) == 1 && call_count == 1);
	expect_record(0, 0, NULL, 0, ACT_QUERY);
	assert(strstr(last_error, "keysvc version query: Timeout") && !last_output[0]);
	assert(!strstr(last_error, "No keys from this request"));
	assert(!strstr(last_error, "Install") && !strstr(last_error, "restart"));
	reset_transport();
	query_length = 2;
	assert(require_keysvc(NULL, 1) == 1 && call_count == 1);
	expect_record(0, 0, NULL, 0, ACT_QUERY);
	assert(strstr(last_error, "unrecognized reply") && !last_output[0]);
	assert(!strstr(last_error, "Install") && !strstr(last_error, "restart"));
	reset_transport();
	query_version = 1;
	assert(require_keysvc(NULL, 1) == 1 && call_count == 1);
	expect_record(0, 0, NULL, 0, ACT_QUERY);
	assert(strstr(last_output, "keysvc version 1") && strstr(last_output, "compatible no"));
	assert(strstr(last_error, "reports version 1") && strstr(last_error, "Required: version 2"));
	reset_transport();
	query_capacity = 6;
	assert(require_keysvc(NULL, 1) == 1 && call_count == 1);
	expect_record(0, 0, NULL, 0, ACT_QUERY);
	assert(strstr(last_output, "held capacity 6") && strstr(last_output, "compatible no"));
	assert(strstr(last_error, "capacity at least 7"));
}

static void check_cleanup(void)
{
	char *hold[] = { "+ctrl" };
	reset_transport();
	assert(cmd_key(NULL, 1, hold) == 0 && call_count == 2);
	expect_record(1, 0, "ctrl", 0, ACT_KEY_PRESS);
	char *args[] = { "+ctrl", "+a", "wait:10", "b" };
	reset_transport();
	fail_call = 3;
	assert(cmd_key(NULL, 4, args) == 1 && call_count == 5);
	expect_record(3, 0, "a", 0, ACT_KEY_RELEASE);
	expect_record(4, 0, "ctrl", 0, ACT_KEY_RELEASE);
	reset_transport();
	fail_call = 3;
	second_fail_call = 4;
	assert(cmd_key(NULL, 4, args) == 1 && call_count == 5);
	expect_record(4, 0, "ctrl", 0, ACT_KEY_RELEASE);

	char *ambiguous[] = { "+ctrl", "wait:1", "+a" };
	reset_transport();
	fail_call = 3;
	assert(cmd_key(NULL, 3, ambiguous) == 1 && call_count == 4);
	expect_record(3, 0, "ctrl", 0, ACT_KEY_RELEASE);
	reset_transport();
	partial_call = 2;
	assert(cmd_key(NULL, 4, args) == 1 && call_count == 2);

	char *released[] = { "+a", "wait:0", "-a", "wait:0", "b" };
	reset_transport();
	fail_call = 4;
	assert(cmd_key(NULL, 5, released) == 1 && call_count == 4);
	char *cleared[] = { "+a", "wait:0", "release-all", "wait:0", "b" };
	reset_transport();
	fail_call = 4;
	assert(cmd_key(NULL, 5, cleared) == 1 && call_count == 4);

	char *prior[] = { "-ctrl", "+a", "wait:1", "b" };
	reset_transport();
	fail_call = 3;
	assert(cmd_key(NULL, 4, prior) == 1 && call_count == 4);
	expect_record(3, 0, "a", 0, ACT_KEY_RELEASE);
}

static void check_interrupts(void)
{
	char *args[] = { "+ctrl", "wait:100", "a" };
	struct sigaction before_int, before_term, after;
	sigaction(SIGINT, NULL, &before_int);
	sigaction(SIGTERM, NULL, &before_term);
	reset_transport();
	wait_signal = SIGTERM;
	assert(cmd_key(NULL, 3, args) == 128 + SIGTERM && call_count == 3);
	expect_record(2, 0, "ctrl", 0, ACT_KEY_RELEASE);
	sigaction(SIGINT, NULL, &after);
	assert(after.sa_handler == before_int.sa_handler);
	sigaction(SIGTERM, NULL, &after);
	assert(after.sa_handler == before_term.sa_handler);
	reset_transport();
	signal_call = 2;
	assert(cmd_key(NULL, 3, args) == 128 + SIGINT && call_count == 3 && wait_count == 0);
	expect_record(2, 0, "ctrl", 0, ACT_KEY_RELEASE);
}

static void check_recovery_warning(void)
{
	char *args[] = { "down" };
	reset_transport();
	fail_call = 2;
	assert(cmd_key(NULL, 1, args) == 1);
	assert(strstr(last_error, "key state may be unknown"));
	assert(strstr(last_error, "Reconnect") && strstr(last_error, "nsptool key release-all"));
	assert(call_count == 2 && calls[1].count == 1);
	expect_record(1, 0, "down", 0, ACT_KEY_TAP);
	assert(!strstr(last_error, "No keys from this request"));
	reset_transport();
	partial_call = 2;
	assert(cmd_key(NULL, 1, args) == 1);
	assert(strstr(last_error, "key state may be unknown") && strstr(last_error, "release-all"));
	reset_transport();
	malformed_call = 2;
	assert(cmd_key(NULL, 1, args) == 1);
	assert(strstr(last_error, "key state may be unknown") && strstr(last_error, "release-all"));
}

static void check_type(void)
{
	reset_transport();
	assert(cmd_type(NULL, "A2\n") == 0 && call_count == 2 && calls[1].count == 3);
	expect_record(1, 0, "a", MOD_SHIFT, ACT_KEY_TAP);
	expect_record(1, 1, "2", 0, ACT_KEY_TAP);
	expect_record(1, 2, "enter", 0, ACT_KEY_TAP);
	reset_transport();
	assert(cmd_type(NULL, "a~") == 2 && call_count == 0);
	assert(cmd_type(NULL, "") == 2 && call_count == 0);
}

static void check_restart(void)
{
	reset_transport();
	assert(cmd_restart(NULL) == 0 && call_count == 1 && wait_count == 0);
	assert(calls[0].count == 1);
	const struct keyrec *request = &calls[0].recs[0];
	assert(request->action == ACT_RESTART && request->modifiers == 0);
	assert((request->code_lo | request->code_hi << 8) == KEYSVC_RESTART_MAGIC);
	assert(strstr(last_output, "accepted") && strstr(last_output, "boot completion unverified"));
	char *single[] = { "restart" };
	reset_transport();
	assert(cmd_key(NULL, 1, single) == 0 && call_count == 1);
	assert(calls[0].recs[0].action == ACT_RESTART);
	reset_transport();
	partial_call = 1;
	assert(cmd_restart(NULL) == 1 && call_count == 1);
	assert(strstr(last_error, "unsupported") && strstr(last_error, "helper") && strstr(last_error, "hardware"));
	reset_transport();
	fail_call = 1;
	assert(cmd_restart(NULL) == 1 && call_count == 1 && wait_count == 0);
	assert(strstr(last_error, "restart is unconfirmed") && strstr(last_error, "before retrying"));
	reset_transport();
	malformed_call = 1;
	assert(cmd_restart(NULL) == 1 && call_count == 1 && wait_count == 0);
	assert(strstr(last_error, "restart is unconfirmed") && strstr(last_error, "Check the calculator"));
	reset_transport();
	extra_reply_call = 1;
	assert(cmd_restart(NULL) == 1 && call_count == 1 && wait_count == 0);
	assert(strstr(last_error, "restart is unconfirmed") && strstr(last_error, "before retrying"));
	char *queued[] = { "down", "wait:1", "restart" };
	reset_transport();
	assert(cmd_key(NULL, 3, queued) == 2 && call_count == 0 && wait_count == 0);
	char *prefix[] = { "restart", "down" };
	assert(cmd_key(NULL, 2, prefix) == 2 && call_count == 0);
	char *chord[] = { "a+restart" };
	assert(cmd_key(NULL, 1, chord) == 2 && call_count == 0);
	char *modifier[] = { "ctrl+restart" };
	assert(cmd_key(NULL, 1, modifier) == 2 && call_count == 0);
}

int main(void)
{
	check_waits();
	check_chords();
	check_validation();
	check_frozen_codes();
	check_known_releases();
	check_batches();
	check_versions();
	check_predispatch();
	check_status();
	check_cleanup();
	check_interrupts();
	check_recovery_warning();
	check_type();
	check_restart();
	puts("nsptool: queue, chord, validation, version, cleanup, interrupt, type, and restart checks passed");
	return 0;
}
