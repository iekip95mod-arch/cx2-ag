#include <os.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "protocol.h"
#include "restart.h"

// keysvc: a small test-automation helper for the owner's own CX II. It registers a NavNet
// service on the calculator, receives key records from the host over the existing USB link,
// and forwards them to the OS's send_key_event so the build loop can drive the UI hands-free.
//
// TI_NN_StartService is an Ndl syscall on every OS in its table. The CX II tables name it
// twice, once as the public thunk (0x100bb24c on 6.4.0.74, a single branch) and once as the
// function it branches to, and the table carries the latter. An earlier version reached the thunk
// by hand-carried address for two OS versions only.
//
// Key codes are the OS's own, (key << 8) | ascii, taken from its keymap: the same values its
// keypad driver passes to send_key_event, so the events look like ordinary key presses.

#ifndef KEYSVC_SID
#define KEYSVC_SID 0x4B45
#endif
#define READ_TIMEOUT_MS 30000

static FILE *logfile;

static const char *log_path(void)
{
#ifdef LOG_PATH
	return LOG_PATH;
#else
	struct stat statbuf;
	return stat("/appdata/ndl/ndl_resources.tns", &statbuf) == 0
		? "/appdata/ndl/keysvc.txt.tns" : "/documents/ndl/keysvc.txt.tns";
#endif
}

static void logline(const char *fmt, ...)
{
	va_list ap;

	if (!logfile)
		return;
	va_start(ap, fmt);
	vfprintf(logfile, fmt, ap);
	va_end(ap);
	fflush(logfile);
}

static void post_key(unsigned short code, unsigned short modifiers, BOOL up, unsigned short scan)
{
	struct s_ns_event ev;

	code = keysvc_navigation_code(code);

	memset(&ev, 0, sizeof ev);
	ev.modifiers = modifiers;
	// The OS matches releases by a 16-bit scan at offset 26, despite the SDK field name.
	memcpy((unsigned char *)&ev + 26, &scan, sizeof scan);
	send_key_event(&ev, code, up, TRUE);
}

struct held_key {
	unsigned short index, code;
	unsigned char modifiers;
};

struct key_state {
	struct held_key keys[KEYSVC_MAX_HELD];
	unsigned count;
};

static struct key_state held;

static unsigned char held_modifiers(const struct key_state *state)
{
	unsigned char modifiers = 0;
	for (unsigned i = 0; i < state->count; i++) {
		const struct held_key *key = &state->keys[i];
		modifiers |= key->modifiers;
		if (keydefs[key->index].plain == 0xAA00)
			modifiers |= MOD_CTRL;
		if (keydefs[key->index].plain == 0xAB00)
			modifiers |= MOD_SHIFT;
	}
	return modifiers;
}

static void release_key(struct key_state *state, unsigned slot, BOOL post)
{
	struct held_key key = state->keys[slot];
	state->count--;
	for (unsigned i = slot; i < state->count; i++)
		state->keys[i] = state->keys[i + 1];
	if (post)
		post_key(key.code, held_modifiers(state), TRUE, 0x4000 + key.index);
}

static BOOL apply_key(struct key_state *state, const struct keyrec *rec, BOOL post)
{
	unsigned index = rec->code_lo | (rec->code_hi << 8);
	if (rec->action == ACT_RELEASE_ALL) {
		if (index || rec->modifiers)
			return FALSE;
		while (state->count)
			release_key(state, state->count - 1, post);
		return TRUE;
	}
	if (index >= KEYSVC_KEY_COUNT ||
	    (rec->modifiers != 0 && rec->modifiers != MOD_SHIFT &&
	     rec->modifiers != MOD_CTRL && rec->modifiers != (MOD_SHIFT | MOD_CTRL)))
		return FALSE;
	index = keysvc_key_index(index);
	unsigned slot = 0;
	while (slot < state->count && state->keys[slot].index != index)
		slot++;
	if (rec->action == ACT_KEY_RELEASE) {
		if (slot == state->count)
			return FALSE;
		release_key(state, slot, post);
		return TRUE;
	}
	if ((rec->action != ACT_KEY_TAP && rec->action != ACT_KEY_PRESS) ||
	    slot != state->count || state->count == KEYSVC_MAX_HELD)
		return FALSE;
	unsigned char modifiers = held_modifiers(state) | rec->modifiers;
	unsigned short code = keysvc_key_code(index, modifiers);
	for (unsigned i = 0; i < state->count; i++)
		if ((state->keys[i].code >> 8) == (code >> 8))
			return FALSE;
	state->keys[state->count++] = (struct held_key){ index, code, rec->modifiers };
	if (post)
		post_key(code, held_modifiers(state), FALSE, 0x4000 + index);
	if (rec->action == ACT_KEY_TAP)
		release_key(state, slot, post);
	return TRUE;
}

static unsigned apply_extended(const unsigned char *bytes, unsigned len, BOOL post)
{
	if (len % sizeof(struct keyrec) || len > KEYSVC_MAX_RECORDS * sizeof(struct keyrec))
		return 0;
	struct key_state next = held;
	for (unsigned off = 0; off < len; off += sizeof(struct keyrec))
		if (!apply_key(&next, (const struct keyrec *)(bytes + off), FALSE))
			return 0;
	if (post)
		for (unsigned off = 0; off < len; off += sizeof(struct keyrec))
			apply_key(&held, (const struct keyrec *)(bytes + off), TRUE);
	return len / sizeof(struct keyrec);
}

// Walks the records, posting them when post is set and only counting them otherwise.
static unsigned apply(const unsigned char *buf, unsigned len, BOOL post)
{
	unsigned done = 0;
	for (unsigned off = 0; off + sizeof(struct keyrec) <= len; off += sizeof(struct keyrec))
		if (buf[off + 3] >= ACT_KEY_TAP && buf[off + 3] <= ACT_QUERY)
			return apply_extended(buf, len, post);
	if (held.count)
		return 0;
	for (unsigned off = 0; off + sizeof(struct keyrec) <= len; off += sizeof(struct keyrec))
		if (buf[off + 3] == ACT_RESTART)
			return 0;

	for (unsigned off = 0; off + sizeof(struct keyrec) <= len; off += sizeof(struct keyrec)) {
		const struct keyrec *rec = (const struct keyrec *)(buf + off);
		unsigned short code = rec->code_lo | (rec->code_hi << 8);

		switch (rec->action) {
		case ACT_TAP:
			if (post) {
				post_key(code, rec->modifiers, FALSE, 0x4100 + (code >> 8));
				post_key(code, 0, TRUE, 0x4100 + (code >> 8));
			}
			break;
		case ACT_PRESS:
			if (post)
				post_key(code, rec->modifiers, FALSE, 0x4100 + (code >> 8));
			break;
		case ACT_RELEASE:
			if (post)
				post_key(code, 0, TRUE, 0x4100 + (code >> 8));
			break;
		default:
			continue;
		}
		done++;
	}
	return done;
}

// One packet per connection, answered with the count of records accepted, and the keys go out
// last: this frame runs on the task that processes them, and blocking here after a key that
// opens a document brought it back with its saved registers overwritten. The reply is a
// receipt for acceptance, not completion.
static unsigned char buf[512];
static uint32_t got;

static void serve(nn_ch_t ch, void *data)
{
	(void)data;
	got = 0;
	logline("serve: entered\n");
	int16_t status = (int16_t)TI_NN_Read(ch, READ_TIMEOUT_MS, buf, sizeof buf, (uint32_t)&got);
	logline("read: status %d got %u\n", status, (unsigned)got);
	if (status < 0 || got == 0)
		return;

	if (got == sizeof(struct keyrec) && !buf[0] && !buf[1] && !buf[2] && buf[3] == ACT_QUERY) {
		unsigned char reply[4] = { 'k', 1, KEYSVC_VERSION, KEYSVC_MAX_HELD };
		status = TI_NN_Write(ch, reply, sizeof reply);
		logline("query: %u bytes, write %d\n", (unsigned)sizeof reply, status);
		return;
	}
	if (got == sizeof(struct keyrec) && buf[0] == (KEYSVC_RESTART_MAGIC & 0xFF) &&
	    buf[1] == (KEYSVC_RESTART_MAGIC >> 8) && !buf[2] && buf[3] == ACT_RESTART) {
		unsigned char reply[2] = { 'k', nl_hwsubtype() == 2 };
		status = TI_NN_Write(ch, reply, sizeof reply);
		if (status >= 0 && reply[1])
			restart_handheld();
		return;
	}
	unsigned accepted = apply(buf, got, FALSE);
	unsigned char reply[2] = { 'k', (unsigned char)accepted };
	status = TI_NN_Write(ch, reply, sizeof reply);
	logline("batch: %u bytes, %u accepted, write %d\n", (unsigned)got, accepted, status);
	if (status >= 0)
		apply(buf, got, TRUE);
}

int main(void)
{
	logfile = fopen(log_path(), "w");
	logline("keysvc: startup=%d hwsubtype=%u\n", nl_isstartup(), nl_hwsubtype());

	int16_t rc = TI_NN_StartService(KEYSVC_SID, NULL, serve);
	logline("keysvc: TI_NN_StartService(0x%04x) = %d\n", KEYSVC_SID, rc);
	if (logfile)
		fflush(logfile);

	if (rc != 1) {
		if (logfile)
			fclose(logfile);
		return 0;
	}

	// The callback lives in this program's memory, so it has to stay mapped after this returns.
	// A resident program must leave through _exit, not return: crt0 cleanup on a normal return
	// tears down state the resident callback still needs (nucleus.h).
	nl_set_resident();
	_exit(0);
}
