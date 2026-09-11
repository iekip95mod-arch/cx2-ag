#include <assert.h>

#define main keysvc_main
#include "../keysvc.c"
#undef main

struct posted_key {
	struct s_ns_event event;
	unsigned short code;
	BOOL up;
	BOOL repeat;
};

static struct posted_key posted[512];
static unsigned posted_count;
static BOOL in_service;
static const void *incoming;
static unsigned incoming_length;
static int16_t read_status, write_status;
static unsigned read_count, write_count, reply_length, posted_at_read;
static unsigned char reply_bytes[4];
static unsigned test_subtype = 2;
static BOOL checking_log, entered_before_read, read_logged_before_write;

static void read_log(char *text, size_t capacity)
{
	long position = ftell(logfile);
	assert(position >= 0 && fseek(logfile, 0, SEEK_SET) == 0);
	size_t length = fread(text, 1, capacity - 1, logfile);
	assert(!ferror(logfile));
	text[length] = 0;
	assert(fseek(logfile, position, SEEK_SET) == 0);
}

void send_key_event(struct s_ns_event *event, unsigned short code, BOOL up, BOOL repeat)
{
	assert(posted_count < sizeof posted / sizeof posted[0]);
	if (in_service)
		assert(write_count == 1 && write_status >= 0);
	memcpy(&posted[posted_count].event, event, sizeof *event);
	posted[posted_count].code = code;
	posted[posted_count].up = up;
	posted[posted_count++].repeat = repeat;
}

int16_t TI_NN_Read(nn_ch_t channel, unsigned timeout, void *bytes, unsigned capacity, uint32_t received)
{
	assert(in_service && channel == &held);
	assert(timeout == READ_TIMEOUT_MS && capacity == sizeof buf);
	assert(received == (uint32_t)(uintptr_t)&got);
	assert(incoming_length <= capacity && read_count++ == 0);
	if (checking_log) {
		char text[256];
		read_log(text, sizeof text);
		entered_before_read = !strcmp(text, "serve: entered\n");
	}
	posted_at_read = posted_count;
	if (incoming_length)
		memcpy(bytes, incoming, incoming_length);
	got = incoming_length;
	return read_status;
}

int16_t TI_NN_Write(nn_ch_t channel, const void *bytes, unsigned length)
{
	assert(in_service && channel == &held && read_count == 1);
	assert(posted_count == posted_at_read && write_count++ == 0);
	if (checking_log) {
		char text[256];
		read_log(text, sizeof text);
		read_logged_before_write = !strcmp(text, "serve: entered\nread: status 0 got 4\n");
	}
	assert(length <= sizeof reply_bytes);
	memcpy(reply_bytes, bytes, length);
	reply_length = length;
	return write_status;
}

int16_t TI_NN_StartService(unsigned short service, void *context, void (*callback)(nn_ch_t, void *))
{
	(void)service;
	(void)context;
	(void)callback;
	assert(!"unexpected service registration");
	return -1;
}

int nl_isstartup(void)
{
	assert(!"unexpected startup query");
	return 0;
}

unsigned nl_hwsubtype(void)
{
	return test_subtype;
}

void nl_set_resident(void)
{
	assert(!"unexpected resident registration");
}

static void expect_key(unsigned index, unsigned short code, unsigned short modifiers, BOOL up,
		       unsigned short scan)
{
	struct s_ns_event expected;
	memset(&expected, 0, sizeof expected);
	expected.modifiers = modifiers;
	assert(scan != 0);
	memcpy((unsigned char *)&expected + 26, &scan, sizeof scan);
	assert(index < posted_count);
	assert(posted[index].code == code);
	assert(posted[index].up == up);
	assert(posted[index].repeat == TRUE);
	assert(memcmp(&posted[index].event, &expected, sizeof expected) == 0);
}

static void check_actions(unsigned short input, unsigned short output, unsigned char modifiers)
{
	unsigned char record[4] = { input & 0xFF, input >> 8, modifiers, ACT_TAP };
	unsigned short scan = 0x4100 + (input >> 8);
	posted_count = 0;
	assert(apply(record, sizeof record, TRUE) == 1);
	assert(posted_count == 2);
	expect_key(0, output, modifiers, FALSE, scan);
	expect_key(1, output, 0, TRUE, scan);

	posted_count = 0;
	record[3] = ACT_PRESS;
	assert(apply(record, sizeof record, TRUE) == 1);
	assert(posted_count == 1);
	expect_key(0, output, modifiers, FALSE, scan);

	record[3] = ACT_RELEASE;
	assert(apply(record, sizeof record, TRUE) == 1);
	assert(posted_count == 2);
	expect_key(0, output, modifiers, FALSE, scan);
	expect_key(1, output, 0, TRUE, scan);
}

static void check_acceptance(void)
{
	const unsigned char records[] = {
		0x00, 0x75, 4, ACT_TAP,
		0x00, 0x71, 3, ACT_PRESS,
		0x00, 0x77, 3, ACT_RELEASE,
		0x00, 0x73, 0, 3,
		0x00, 0x73, 0, 255,
		0x00, 0x75, 0
	};
	posted_count = 0;
	assert(apply(records, sizeof records, FALSE) == 3);
	assert(posted_count == 0);
	assert(apply(records, sizeof records, TRUE) == 3);
	assert(posted_count == 4);
	expect_key(0, 0xF400, 4, FALSE, 0x4175);
	expect_key(1, 0xF400, 0, TRUE, 0x4175);
	expect_key(2, 0xF300, 3, FALSE, 0x4171);
	expect_key(3, 0xF100, 0, TRUE, 0x4177);

	for (unsigned length = 0; length < sizeof(struct keyrec); ++length) {
		posted_count = 0;
		assert(apply(records, length, FALSE) == 0);
		assert(apply(records, length, TRUE) == 0);
		assert(posted_count == 0);
	}
}

static void reset(void)
{
	memset(&held, 0, sizeof held);
	posted_count = 0;
	keysvc_test_restart_register = 0;
	test_subtype = 2;
}

static unsigned key_index(const char *name)
{
	for (unsigned i = 0; i < KEYSVC_KEY_COUNT; i++)
		if (!strcmp(keydefs[i].name, name))
			return i;
	assert(!"unknown test key");
	return 0;
}

static struct keyrec key_record(const char *name, unsigned char modifiers, unsigned char action)
{
	unsigned index = key_index(name);
	return (struct keyrec){ index & 0xFF, index >> 8, modifiers, action };
}

static void expect_logical(unsigned event, const char *name, unsigned short code,
			   unsigned short modifiers, BOOL up)
{
	expect_key(event, code, modifiers, up, 0x4000 + key_index(name));
}

static void send_key(const char *name, unsigned char modifiers, unsigned char action)
{
	struct keyrec record = key_record(name, modifiers, action);
	assert(apply((const unsigned char *)&record, sizeof record, TRUE) == 1);
}

static void expect_rejected(const void *records, unsigned length)
{
	struct key_state before = held;
	unsigned events_before = posted_count;
	assert(apply(records, length, FALSE) == 0);
	assert(posted_count == events_before && !memcmp(&held, &before, sizeof held));
	assert(apply(records, length, TRUE) == 0);
	assert(posted_count == events_before && !memcmp(&held, &before, sizeof held));
}

static void check_held_keys(void)
{
	reset();
	struct keyrec pair[] = {
		key_record("a", 0, ACT_KEY_PRESS),
		key_record("b", 0, ACT_KEY_PRESS)
	};
	struct key_state before = held;
	assert(apply((const unsigned char *)pair, sizeof pair, FALSE) == 2);
	assert(posted_count == 0 && !memcmp(&held, &before, sizeof held));
	assert(apply((const unsigned char *)pair, sizeof pair, TRUE) == 2);
	assert(held.count == 2 && posted_count == 2);
	expect_logical(0, "a", 0x6661, 0, FALSE);
	expect_logical(1, "b", 0x4662, 0, FALSE);
	send_key("b", 0, ACT_KEY_RELEASE);
	assert(held.count == 1 && held.keys[0].index == key_index("a"));
	assert(held.keys[0].code == 0x6661 && posted_count == 3);
	expect_logical(2, "b", 0x4662, 0, TRUE);
	send_key("c", 0, ACT_KEY_TAP);
	assert(held.count == 1 && held.keys[0].index == key_index("a"));
	expect_logical(3, "c", 0x2663, 0, FALSE);
	expect_logical(4, "c", 0x2663, 0, TRUE);
	send_key("a", 0, ACT_KEY_RELEASE);
	assert(held.count == 0 && posted_count == 6);
	expect_logical(5, "a", 0x6661, 0, TRUE);
	reset();
	send_key("on", 0, ACT_KEY_PRESS);
	send_key("home", 0, ACT_KEY_RELEASE);
	assert(held.count == 0 && posted_count == 2);
	expect_logical(0, "home", 0xFD00, 0, FALSE);
	expect_logical(1, "home", 0xFD00, 0, TRUE);
}

static void check_modifiers(void)
{
	reset();
	send_key("ctrl", 0, ACT_KEY_PRESS);
	send_key("shift", 0, ACT_KEY_PRESS);
	send_key("a", 0, ACT_KEY_PRESS);
	send_key("ctrl", 0, ACT_KEY_RELEASE);
	send_key("a", 0, ACT_KEY_RELEASE);
	send_key("shift", 0, ACT_KEY_RELEASE);
	assert(held.count == 0 && posted_count == 6);
	expect_logical(0, "ctrl", 0xAA00, 4, FALSE);
	expect_logical(1, "shift", 0xAB00, 7, FALSE);
	expect_logical(2, "a", 0xB001, 7, FALSE);
	expect_logical(3, "ctrl", 0xAA00, 3, TRUE);
	expect_logical(4, "a", 0xB001, 3, TRUE);
	expect_logical(5, "shift", 0xAB00, 0, TRUE);

	reset();
	send_key("shift", 0, ACT_KEY_PRESS);
	send_key("a", 0, ACT_KEY_PRESS);
	send_key("shift", 0, ACT_KEY_RELEASE);
	send_key("ctrl", 0, ACT_KEY_PRESS);
	send_key("a", 0, ACT_KEY_RELEASE);
	send_key("b", 0, ACT_KEY_TAP);
	send_key("ctrl", 0, ACT_KEY_RELEASE);
	assert(held.count == 0 && posted_count == 8);
	expect_logical(0, "shift", 0xAB00, 3, FALSE);
	expect_logical(1, "a", 0x6641, 3, FALSE);
	expect_logical(2, "shift", 0xAB00, 0, TRUE);
	expect_logical(3, "ctrl", 0xAA00, 4, FALSE);
	expect_logical(4, "a", 0x6641, 4, TRUE);
	expect_logical(5, "b", 0xB102, 4, FALSE);
	expect_logical(6, "b", 0xB102, 4, TRUE);
	expect_logical(7, "ctrl", 0xAA00, 0, TRUE);

	reset();
	send_key("a", 4, ACT_KEY_PRESS);
	send_key("b", 3, ACT_KEY_PRESS);
	send_key("down", 0, ACT_KEY_TAP);
	send_key("a", 0, ACT_KEY_RELEASE);
	send_key("b", 0, ACT_KEY_RELEASE);
	assert(held.count == 0 && posted_count == 6);
	expect_logical(0, "a", 0xB001, 4, FALSE);
	expect_logical(1, "b", 0xB102, 7, FALSE);
	expect_logical(2, "down", 0xF400, 7, FALSE);
	expect_logical(3, "down", 0xF400, 7, TRUE);
	expect_logical(4, "a", 0xB001, 3, TRUE);
	expect_logical(5, "b", 0xB102, 0, TRUE);

	reset();
	send_key("a", 0, ACT_KEY_PRESS);
	send_key("ctrl", 0, ACT_KEY_PRESS);
	send_key("a", 7, ACT_KEY_RELEASE);
	send_key("ctrl", 0, ACT_KEY_RELEASE);
	assert(held.count == 0 && posted_count == 4);
	expect_logical(0, "a", 0x6661, 0, FALSE);
	expect_logical(1, "ctrl", 0xAA00, 4, FALSE);
	expect_logical(2, "a", 0x6661, 4, TRUE);
	expect_logical(3, "ctrl", 0xAA00, 0, TRUE);
}

static void check_release_all(void)
{
	reset();
	send_key("ctrl", 0, ACT_KEY_PRESS);
	send_key("shift", 0, ACT_KEY_PRESS);
	send_key("a", 0, ACT_KEY_PRESS);
	send_key("down", 0, ACT_KEY_PRESS);
	const struct keyrec release = { 0, 0, 0, ACT_RELEASE_ALL };
	struct key_state before = held;
	assert(apply((const unsigned char *)&release, sizeof release, FALSE) == 1);
	assert(posted_count == 4 && !memcmp(&held, &before, sizeof held));
	assert(apply((const unsigned char *)&release, sizeof release, TRUE) == 1);
	assert(held.count == 0 && posted_count == 8);
	expect_logical(4, "down", 0xF400, 7, TRUE);
	expect_logical(5, "a", 0xB001, 7, TRUE);
	expect_logical(6, "shift", 0xAB00, 4, TRUE);
	expect_logical(7, "ctrl", 0xAA00, 0, TRUE);
	assert(apply((const unsigned char *)&release, sizeof release, TRUE) == 1);
	assert(held.count == 0 && posted_count == 8);
}

static void check_rejections(void)
{
	reset();
	struct keyrec duplicate[] = {
		key_record("a", 0, ACT_KEY_PRESS),
		key_record("a", 0, ACT_KEY_PRESS)
	};
	expect_rejected(duplicate, sizeof duplicate);
	struct keyrec aliases[] = {
		key_record("q", 0, ACT_KEY_PRESS),
		key_record("u", 0, ACT_KEY_PRESS)
	};
	expect_rejected(aliases, sizeof aliases);
	send_key("q", 0, ACT_KEY_PRESS);
	expect_rejected(aliases + 1, sizeof aliases[1]);
	struct keyrec record = key_record("q", 0, ACT_KEY_TAP);
	expect_rejected(&record, sizeof record);
	record.action = ACT_KEY_PRESS;
	expect_rejected(&record, sizeof record);
	record = key_record("b", 0, ACT_KEY_RELEASE);
	expect_rejected(&record, sizeof record);
	const struct keyrec legacy = { 0x0D, 0x10, 0, ACT_TAP };
	expect_rejected(&legacy, sizeof legacy);
	reset();
	struct keyrec mixed[] = { legacy, key_record("b", 0, ACT_KEY_TAP) };
	expect_rejected(mixed, sizeof mixed);
	mixed[0] = mixed[1];
	mixed[1] = legacy;
	expect_rejected(mixed, sizeof mixed);

	const unsigned char invalid_modifiers[] = { 1, 2, 5, 6, 8, 255 };
	struct keyrec invalid[] = {
		key_record("a", 0, ACT_KEY_PRESS),
		key_record("b", 0, ACT_KEY_TAP)
	};
	for (unsigned i = 0; i < sizeof invalid_modifiers; i++) {
		invalid[1].modifiers = invalid_modifiers[i];
		expect_rejected(invalid, sizeof invalid);
	}
	invalid[1] = key_record("b", 0, ACT_KEY_TAP);
	invalid[1].code_lo = KEYSVC_KEY_COUNT & 0xFF;
	invalid[1].code_hi = KEYSVC_KEY_COUNT >> 8;
	expect_rejected(invalid, sizeof invalid);
	invalid[1].code_lo = invalid[1].code_hi = 255;
	expect_rejected(invalid, sizeof invalid);
	const unsigned char invalid_actions[] = { 3, 15, 20, 21, 255 };
	for (unsigned i = 0; i < sizeof invalid_actions; i++) {
		invalid[1] = key_record("b", 0, invalid_actions[i]);
		expect_rejected(invalid, sizeof invalid);
	}
	invalid[1] = (struct keyrec){ 1, 0, 0, ACT_RELEASE_ALL };
	expect_rejected(invalid, sizeof invalid);
	invalid[1] = (struct keyrec){ 0, 0, 4, ACT_RELEASE_ALL };
	expect_rejected(invalid, sizeof invalid);
	invalid[1] = key_record("b", 0, ACT_KEY_TAP);
	for (unsigned length = 1; length < sizeof invalid; length++)
		if (length != sizeof(struct keyrec))
			expect_rejected(invalid, length);
	struct keyrec oversized[KEYSVC_MAX_RECORDS + 1];
	for (unsigned i = 0; i < sizeof oversized / sizeof oversized[0]; i++)
		oversized[i] = key_record("a", 0, ACT_KEY_TAP);
	expect_rejected(oversized, sizeof oversized);
	assert(apply((const unsigned char *)oversized, sizeof oversized - sizeof oversized[0], TRUE)
	       == KEYSVC_MAX_RECORDS);
	assert(held.count == 0 && posted_count == KEYSVC_MAX_RECORDS * 2);
}

static void check_capacity(void)
{
	reset();
	const char *names[] = { "a", "b", "c", "d", "e", "f", "g", "h" };
	struct keyrec records[8];
	assert(KEYSVC_MAX_HELD == 7);
	for (unsigned i = 0; i < 8; i++)
		records[i] = key_record(names[i], 0, ACT_KEY_PRESS);
	expect_rejected(records, sizeof records);
	assert(apply((const unsigned char *)records, 7 * sizeof records[0], TRUE) == 7);
	assert(held.count == 7 && posted_count == 7);
	expect_rejected(records + 7, sizeof records[7]);
	records[7].action = ACT_KEY_TAP;
	expect_rejected(records + 7, sizeof records[7]);
	send_key("d", 0, ACT_KEY_RELEASE);
	send_key("h", 0, ACT_KEY_PRESS);
	assert(held.count == 7 && posted_count == 9);
	expect_logical(7, "d", 0x8564, 0, TRUE);
	expect_logical(8, "h", 0x8468, 0, FALSE);
	const struct keyrec release = { 0, 0, 0, ACT_RELEASE_ALL };
	assert(apply((const unsigned char *)&release, sizeof release, TRUE) == 1);
	assert(held.count == 0 && posted_count == 16);
	const char *released[] = { "h", "g", "f", "e", "c", "b", "a" };
	const unsigned short codes[] = { 0x8468, 0x2567, 0x4566, 0x6565, 0x2663, 0x4662, 0x6661 };
	for (unsigned i = 0; i < 7; i++)
		expect_logical(9 + i, released[i], codes[i], 0, TRUE);
}

static void service_packet(const void *records, unsigned length, int16_t reading, int16_t writing)
{
	incoming = records;
	incoming_length = length;
	read_status = reading;
	write_status = writing;
	read_count = write_count = reply_length = 0;
	in_service = TRUE;
	serve(&held, NULL);
	in_service = FALSE;
	assert(read_count == 1);
}

static void check_service(void)
{
	reset();
	struct keyrec records[] = {
		key_record("a", 0, ACT_KEY_PRESS),
		key_record("b", 0, ACT_KEY_PRESS)
	};
	service_packet(records, sizeof records, 0, -1);
	assert(write_count == 1 && reply_length == 2);
	assert(reply_bytes[0] == 'k' && reply_bytes[1] == 2);
	assert(held.count == 0 && posted_count == 0);
	service_packet(records, sizeof records, 0, 0);
	assert(write_count == 1 && reply_length == 2);
	assert(reply_bytes[0] == 'k' && reply_bytes[1] == 2);
	assert(held.count == 2 && posted_count == 2);
	expect_logical(0, "a", 0x6661, 0, FALSE);
	expect_logical(1, "b", 0x4662, 0, FALSE);
	struct key_state before = held;
	const struct keyrec query = { 0, 0, 0, ACT_QUERY };
	service_packet(&query, sizeof query, 0, 0);
	const unsigned char query_reply[] = { 'k', 1, 2, 7 };
	assert(write_count == 1 && reply_length == sizeof query_reply);
	assert(!memcmp(reply_bytes, query_reply, sizeof query_reply));
	assert(posted_count == 2 && !memcmp(&held, &before, sizeof held));
	struct keyrec release = key_record("b", 0, ACT_KEY_RELEASE);
	service_packet(&release, sizeof release, 0, -1);
	assert(write_count == 1 && reply_length == 2 && reply_bytes[1] == 1);
	assert(posted_count == 2 && !memcmp(&held, &before, sizeof held));
	service_packet(records, sizeof records, 0, 0);
	assert(write_count == 1 && reply_length == 2 && reply_bytes[1] == 0);
	assert(posted_count == 2 && !memcmp(&held, &before, sizeof held));
	service_packet(&release, sizeof release, -1, 0);
	assert(write_count == 0 && posted_count == 2 && !memcmp(&held, &before, sizeof held));
	service_packet(NULL, 0, 0, 0);
	assert(write_count == 0 && posted_count == 2 && !memcmp(&held, &before, sizeof held));
	service_packet(&release, sizeof release, 0, 0);
	assert(write_count == 1 && reply_length == 2 && reply_bytes[1] == 1);
	assert(held.count == 1 && held.keys[0].index == key_index("a") && posted_count == 3);
	expect_logical(2, "b", 0x4662, 0, TRUE);
	const struct keyrec release_all = { 0, 0, 0, ACT_RELEASE_ALL };
	service_packet(&release_all, sizeof release_all, 0, 0);
	assert(write_count == 1 && reply_length == 2 && reply_bytes[1] == 1);
	assert(held.count == 0 && posted_count == 4);
	expect_logical(3, "a", 0x6661, 0, TRUE);
}

static void check_diagnostics(void)
{
	const struct keyrec query = { 0, 0, 0, ACT_QUERY };
	unsigned missing = 0;
	for (unsigned scenario = 0; scenario < 4; ++scenario) {
		reset();
		logfile = tmpfile();
		assert(logfile);
		checking_log = TRUE;
		entered_before_read = read_logged_before_write = FALSE;
		int16_t reading = scenario == 2 ? -7 : 0;
		int16_t writing = scenario == 1 ? -9 : 0;
		unsigned length = scenario == 3 ? 0 : sizeof query;
		service_packet(&query, length, reading, writing);
		char text[256], expected[256];
		read_log(text, sizeof text);
		if (scenario < 2)
			snprintf(expected, sizeof expected, "serve: entered\nread: status 0 got 4\nquery: 4 bytes, write %d\n", writing);
		else
			snprintf(expected, sizeof expected, "serve: entered\nread: status %d got %u\n", reading, length);
		BOOL observed = entered_before_read && !strcmp(text, expected) &&
			(scenario >= 2 || read_logged_before_write);
		if (!observed) {
			fprintf(stderr, "diagnostic scenario %u missing callback/read/write evidence: [%s]\n", scenario, text);
			++missing;
		}
		assert(write_count == (scenario < 2 ? 1 : 0) && posted_count == 0 && held.count == 0);
		if (scenario < 2) {
			const unsigned char reply[] = { 'k', 1, KEYSVC_VERSION, KEYSVC_MAX_HELD };
			assert(reply_length == sizeof reply && !memcmp(reply_bytes, reply, sizeof reply));
		}
		assert(fclose(logfile) == 0);
		logfile = NULL;
		checking_log = FALSE;
	}
	assert(missing == 0);
}

static void check_restart(void)
{
	reset();
	struct keyrec restart = { KEYSVC_RESTART_MAGIC & 0xFF,
		KEYSVC_RESTART_MAGIC >> 8, 0, ACT_RESTART };
	service_packet(&restart, sizeof restart, 0, -1);
	assert(reply_bytes[1] == 1 && !keysvc_test_restart_register);
	for (test_subtype = 0; test_subtype < 2; test_subtype++) {
		service_packet(&restart, sizeof restart, 0, 0);
		assert(reply_bytes[1] == 0 && !keysvc_test_restart_register);
	}
	for (unsigned i = 0; i < 3; i++) {
		struct keyrec invalid = restart;
		((unsigned char *)&invalid)[i]++;
		service_packet(&invalid, sizeof invalid, 0, 0);
		assert(reply_bytes[1] == 0 && !keysvc_test_restart_register);
	}
	struct keyrec mixed[] = { key_record("a", 0, ACT_KEY_TAP), restart };
	service_packet(mixed, sizeof mixed, 0, 0);
	assert(reply_bytes[1] == 0 && !keysvc_test_restart_register && !posted_count);
	mixed[0] = (struct keyrec){ 0, 0x75, 0, ACT_TAP };
	service_packet(mixed, sizeof mixed, 0, 0);
	assert(reply_bytes[1] == 0 && !keysvc_test_restart_register && !posted_count);
	service_packet(&restart, sizeof restart - 1, 0, 0);
	assert(reply_bytes[1] == 0 && !keysvc_test_restart_register);
	service_packet(&restart, sizeof restart, 0, 0);
	assert(reply_length == 2 && reply_bytes[0] == 'k' && reply_bytes[1] == 1);
	assert(keysvc_test_restart_register == 0x80 && !posted_count);
}

int main(void)
{
	const struct {
		unsigned short input;
		unsigned short output;
	} arrows[] = {
		{ 0x7100, 0xF300 },
		{ 0x7300, 0xF200 },
		{ 0x7500, 0xF400 },
		{ 0x7700, 0xF100 }
	};
	const unsigned char modifiers[] = { 0, 3, 4 };
	const unsigned short ordinary[] = {
		0x100D, 0xA600, 0x5332, 0xD200, 0x436E, 0x434E, 0xBD0E,
		0x7137, 0x7331, 0x753D, 0xF300, 0xF200, 0xF400, 0xF100
	};

	for (unsigned i = 0; i < sizeof arrows / sizeof arrows[0]; ++i)
		for (unsigned j = 0; j < sizeof modifiers / sizeof modifiers[0]; ++j)
			check_actions(arrows[i].input, arrows[i].output, modifiers[j]);
	for (unsigned i = 0; i < sizeof ordinary / sizeof ordinary[0]; ++i)
		for (unsigned j = 0; j < sizeof modifiers / sizeof modifiers[0]; ++j)
			check_actions(ordinary[i], ordinary[i], modifiers[j]);
	check_acceptance();
	check_held_keys();
	check_modifiers();
	check_release_all();
	check_rejections();
	check_capacity();
	check_service();
	check_diagnostics();
	check_restart();
	puts("keysvc: 54 legacy cases, held keys, modifiers, atomic packets, capacity and service checks passed");
	return 0;
}
