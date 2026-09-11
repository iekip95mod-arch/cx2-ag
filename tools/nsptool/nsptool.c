// nsptool: drive a physically connected TI-Nspire from the shell.
//
// TI's own web app reaches the calculator over WebUSB and a WASM build of their link protocol, which
// works but needs a browser, a user gesture for the device picker, and a page whose JS cannot be
// driven from here. libnspire implements the same protocol natively against libusb, so this is the
// same capability without any of that.
//
// Session mode retains one USB connection for serialized requests.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <nspire.h>

// Root is needed for exactly one thing: detaching the IOKit driver so the USB interface can be
// claimed. Everything after that is ordinary bulk transfer on an already-open handle. So the
// privilege is dropped the moment the link is up, before a single argument-supplied path is touched.
//
// This is what makes a NOPASSWD sudoers entry for this binary reasonable. Without it, get would
// write an attacker-chosen path as root, and passwordless sudo on such a tool is a local root hole
// rather than a convenience.
//
// Under sudo the real uid is also 0, so setuid(getuid()) drops nothing. The first version of this
// did exactly that and silently kept root: the screenshot it wrote came out owned by root:wheel,
// which is the only reason the bug was noticed. The invoking user has to come from SUDO_UID.
static int drop_privileges(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();

	// Not root, so there is nothing to drop. The CX II opens without the IOKit detach on this
	// machine, and setgroups would refuse for a plain user anyway.
	if (uid != 0 && geteuid() != 0)
		return 0;

	const char *sudo_uid = getenv("SUDO_UID");
	const char *sudo_gid = getenv("SUDO_GID");
	if (uid == 0 && sudo_uid && sudo_gid) {
		char *end;
		unsigned long value = strtoul(sudo_uid, &end, 10);
		if (*end || !value) {
			fprintf(stderr, "refusing to continue: SUDO_UID is not a usable uid\n");
			return -1;
		}
		uid = (uid_t)value;

		value = strtoul(sudo_gid, &end, 10);
		if (*end) {
			fprintf(stderr, "refusing to continue: SUDO_GID is not a usable gid\n");
			return -1;
		}
		gid = (gid_t)value;
	}

	if (uid == 0) {
		fprintf(stderr, "refusing to continue: running as root with no user to drop to. "
			"Run this through sudo as a normal user rather than as root directly.\n");
		return -1;
	}

	// Supplementary groups first, then gid, then uid. Any other order leaves privilege behind:
	// dropping the uid first removes the ability to do the other two.
	if (setgroups(1, &gid) || setgid(gid) || setuid(uid)) {
		fprintf(stderr, "refusing to continue: could not drop privileges: %s\n",
			strerror(errno));
		return -1;
	}

	// Regaining root must now be impossible. Checking is the point: the failure this guards
	// against is a drop that reports success and does nothing.
	if (setuid(0) == 0 || geteuid() != uid || getuid() != uid) {
		fprintf(stderr, "refusing to continue: privileges survived the drop\n");
		return -1;
	}
	return 0;
}

static int fail(const char *what, int err)
{
	fprintf(stderr, "%s: %s\n", what, nspire_strerror(err));
	return 1;
}

static int finish_output(FILE *out, const char *local, int failed)
{
	int output_error = failed || ferror(out) ? (errno ? errno : EIO) : 0;
	if (fclose(out) != 0 && !output_error)
		output_error = errno ? errno : EIO;
	if (output_error) {
		fprintf(stderr, "cannot write %s: %s\n", local, strerror(output_error));
		return 1;
	}
	return 0;
}

static const char *type_name(enum nspire_type type)
{
	switch (type) {
	case NSPIRE_CAS: return "Nspire CAS";
	case NSPIRE_NONCAS: return "Nspire non-CAS";
	case NSPIRE_CASCX: return "Nspire CX CAS";
	case NSPIRE_NONCASCX: return "Nspire CX non-CAS";
	case NSPIRE_CASCX2: return "Nspire CX II CAS";
	case NSPIRE_NONCASCX2: return "Nspire CX II non-CAS";
	default: return "unknown";
	}
}

static int cmd_info(nspire_handle_t *handle)
{
	struct nspire_devinfo info;
	int err = nspire_device_info(handle, &info);
	if (err)
		return fail("device info", err);

	printf("name          %s\n", info.device_name);
	printf("hw type       %s (0x%02x)\n", type_name(info.hw_type), info.hw_type);
	printf("os version    %u.%u.%u\n", info.versions[NSPIRE_VER_OS].major,
	       info.versions[NSPIRE_VER_OS].minor, info.versions[NSPIRE_VER_OS].build);
	printf("boot1         %u.%u.%u\n", info.versions[NSPIRE_VER_BOOT1].major,
	       info.versions[NSPIRE_VER_BOOT1].minor, info.versions[NSPIRE_VER_BOOT1].build);
	printf("boot2         %u.%u.%u\n", info.versions[NSPIRE_VER_BOOT2].major,
	       info.versions[NSPIRE_VER_BOOT2].minor, info.versions[NSPIRE_VER_BOOT2].build);
	printf("storage       %llu free of %llu\n", (unsigned long long)info.storage.free,
	       (unsigned long long)info.storage.total);
	printf("ram           %llu free of %llu\n", (unsigned long long)info.ram.free,
	       (unsigned long long)info.ram.total);
	printf("lcd           %ux%u %ubpp\n", info.lcd.width, info.lcd.height, info.lcd.bbp);
	printf("battery       %s%s (0x%02x)\n",
	       info.batt.status == NSPIRE_BATT_POWERED ? "external power" :
	       info.batt.status == NSPIRE_BATT_OK ? "ok" :
	       info.batt.status == NSPIRE_BATT_LOW ? "low" : "unknown",
	       info.batt.is_charging ? ", charging" : "", info.batt.status);
	printf("clock         %u\n", info.clock_speed);
	printf("runlevel      %s (%u)\n",
	       info.runlevel == NSPIRE_RUNLEVEL_OS ? "os" :
	       info.runlevel == NSPIRE_RUNLEVEL_OS_CX2 ? "os" :
	       info.runlevel == NSPIRE_RUNLEVEL_RECOVERY ? "recovery" : "unknown",
	       (unsigned)info.runlevel);
	printf("extensions    file=%s os=%s\n", info.extensions.file, info.extensions.os);
	printf("electronic id %s\n", info.electronic_id);
	return 0;
}

static int cmd_ls(nspire_handle_t *handle, const char *path)
{
	struct nspire_dir_info *list;
	int err = nspire_dirlist(handle, path, &list);
	if (err)
		return fail("dirlist", err);

	for (unsigned long i = 0; i < list->num; i++) {
		struct nspire_dir_item *item = &list->items[i];
		printf("%s\t%lu\t%s\n", item->type == NSPIRE_DIR ? "dir" : "file",
		       item->size, item->name);
	}
	nspire_dirlist_free(list);
	return 0;
}

static int cmd_get(nspire_handle_t *handle, const char *remote, const char *local)
{
	struct nspire_dir_item attr;
	int err = nspire_attr(handle, remote, &attr);
	if (err)
		return fail("stat", err);

	void *buffer = malloc(attr.size ? attr.size : 1);
	if (!buffer) {
		fprintf(stderr, "out of memory for %lu bytes\n", attr.size);
		return 1;
	}

	size_t got = 0;
	err = nspire_file_read(handle, remote, buffer, attr.size, &got);
	if (err) {
		free(buffer);
		return fail("read", err);
	}
	if (got > attr.size) {
		fprintf(stderr, "read exceeded reported size for %s: %zu > %lu\n", remote, got, attr.size);
		free(buffer);
		return 1;
	}

	FILE *out = fopen(local, "wb");
	if (!out) {
		fprintf(stderr, "cannot write %s: %s\n", local, strerror(errno));
		free(buffer);
		return 1;
	}
	errno = 0;
	size_t written = fwrite(buffer, 1, got, out);
	int output_failed = finish_output(out, local, written != got);
	free(buffer);

	if (output_failed)
		return 1;
	printf("got %s -> %s (%zu bytes)\n", remote, local, got);
	return 0;
}

static int cmd_put(nspire_handle_t *handle, const char *local, const char *remote)
{
	FILE *in = fopen(local, "rb");
	if (!in) {
		fprintf(stderr, "cannot read %s: %s\n", local, strerror(errno));
		return 1;
	}
	fseek(in, 0, SEEK_END);
	long size = ftell(in);
	rewind(in);
	if (size < 0) {
		fprintf(stderr, "cannot size %s\n", local);
		fclose(in);
		return 1;
	}

	void *buffer = malloc(size ? (size_t)size : 1);
	if (!buffer || fread(buffer, 1, size, in) != (size_t)size) {
		fprintf(stderr, "cannot load %s\n", local);
		free(buffer);
		fclose(in);
		return 1;
	}
	fclose(in);

	int err = nspire_file_write(handle, remote, buffer, size);
	free(buffer);
	if (err)
		return fail("write", err);

	printf("put %s -> %s (%ld bytes)\n", local, remote, size);
	return 0;
}

static int cmd_rm(nspire_handle_t *handle, const char *path)
{
	int err = nspire_file_delete(handle, path);
	if (err)
		return fail("delete", err);
	printf("deleted %s\n", path);
	return 0;
}

static int cmd_mkdir(nspire_handle_t *handle, const char *path)
{
	int err = nspire_dir_create(handle, path);
	if (err)
		return fail("mkdir", err);
	printf("created %s\n", path);
	return 0;
}

static int cmd_rmdir(nspire_handle_t *handle, const char *path)
{
	struct nspire_dir_info *list;
	int err = nspire_dirlist(handle, path, &list);
	if (err)
		return fail("rmdir", err);
	int nonempty = list->num != 0;
	nspire_dirlist_free(list);
	if (nonempty) {
		fprintf(stderr, "rmdir: directory is not empty: %s\n", path);
		return 1;
	}
	err = nspire_dir_delete(handle, path);
	if (err)
		return fail("rmdir", err);
	printf("removed directory %s\n", path);
	return 0;
}

// Written as a binary PPM rather than PNG so this needs no image library. The caller converts if it
// wants something else, and a raw format keeps the pixel values inspectable when a screenshot is
// being used as evidence rather than as a picture.
static int cmd_screenshot(nspire_handle_t *handle, const char *local)
{
	struct nspire_image *image;
	int err = nspire_screenshot(handle, &image);
	if (err)
		return fail("screenshot", err);

	FILE *out = fopen(local, "wb");
	if (!out) {
		fprintf(stderr, "cannot write %s: %s\n", local, strerror(errno));
		free(image);
		return 1;
	}

	errno = 0;
	int output_failed = fprintf(out, "P6\n%u %u\n255\n", image->width, image->height) < 0;
	size_t pixels = (size_t)image->width * image->height;

	if (image->bpp == 16) {
		for (size_t i = 0; i < pixels && !output_failed; i++) {
			uint16_t value = image->data[i * 2] | (image->data[i * 2 + 1] << 8);
			// RGB565 expanded by replicating high bits into the low ones, so full-scale
			// stays full-scale instead of topping out at 248.
			unsigned char red = ((value >> 11) & 0x1F) << 3;
			unsigned char green = ((value >> 5) & 0x3F) << 2;
			unsigned char blue = (value & 0x1F) << 3;
			unsigned char rgb[3] = { red | (red >> 5), green | (green >> 6),
						 blue | (blue >> 5) };
			output_failed = fwrite(rgb, 1, sizeof rgb, out) != sizeof rgb;
		}
	} else if (image->bpp == 8) {
		for (size_t i = 0; i < pixels && !output_failed; i++) {
			unsigned char grey = image->data[i];
			unsigned char rgb[3] = { grey, grey, grey };
			output_failed = fwrite(rgb, 1, sizeof rgb, out) != sizeof rgb;
		}
	} else {
		fprintf(stderr, "unhandled screenshot depth: %u bpp\n", image->bpp);
		fclose(out);
		free(image);
		return 1;
	}

	if (finish_output(out, local, output_failed)) {
		free(image);
		return 1;
	}
	printf("screenshot %ux%u %ubpp -> %s\n", image->width, image->height, image->bpp, local);
	free(image);
	return 0;
}

// Logical key records share the keysvc table and are translated on the calculator.

#include "../keysvc/protocol.h"
#include "key_os.h"

#define MAX_KEY_RECORDS 4096
#define MAX_KEY_WAIT_MS 300000

struct key_step {
	size_t first, count;
	unsigned wait_ms;
};

struct key_hold {
	struct keyrec rec;
	uint16_t code;
};

static volatile sig_atomic_t key_interrupted;

static const struct keydef *find_key(const char *name)
{
	for (size_t i = 0; i < sizeof keydefs / sizeof keydefs[0]; i++)
		if (!strcmp(keydefs[i].name, name))
			return &keydefs[i];
	return NULL;
}

static void set_record(struct keyrec *rec, uint16_t code, uint8_t modifiers, uint8_t action)
{
	rec->code_lo = code & 0xFF;
	rec->code_hi = code >> 8;
	rec->modifiers = modifiers;
	rec->action = action;
}

// A chord expands into a press group followed by releases in reverse order.
static int parse_key_group(const char *spec, struct keyrec *recs, size_t *count)
{
	uint8_t action = ACT_KEY_TAP;
	uint8_t modifiers = 0;
	uint16_t keys[KEYSVC_MAX_HELD + 2];
	size_t key_count = 0, modifier_count = 0;
	char chord[128];
	if (strlen(spec) >= sizeof chord)
		return 0;

	if (*spec == '+') {
		action = ACT_KEY_PRESS;
		spec++;
	} else if (*spec == '-') {
		action = ACT_KEY_RELEASE;
		spec++;
	}
	strcpy(chord, spec);
	for (char *name = chord; name;) {
		char *next = strchr(name, '+');
		if (next)
			*next++ = '\0';
		const struct keydef *key = find_key(name);
		if (!key || key_count == sizeof keys / sizeof keys[0])
			return 0;
		unsigned index = key - keydefs;
		for (size_t i = 0; i < key_count; ++i)
			if (keysvc_key_index(keys[i]) == keysvc_key_index(index))
				return 0;
		uint8_t modifier = key->plain == 0xAA00 ? MOD_CTRL :
			key->plain == 0xAB00 ? MOD_SHIFT : 0;
		if (modifier) {
			if (modifier_count != key_count)
				return 0;
			modifiers |= modifier;
			modifier_count++;
		}
		keys[key_count++] = index;
		name = next;
	}
	if (modifier_count == key_count) {
		modifier_count = 0;
		modifiers = 0;
	}
	key_count -= modifier_count;
	if (!key_count || key_count > KEYSVC_MAX_HELD)
		return 0;
	*count = 0;
	if (action == ACT_KEY_TAP && key_count == 1) {
		set_record(&recs[(*count)++], keys[modifier_count], modifiers, action);
		return 1;
	}
	if (action != ACT_KEY_RELEASE)
		for (size_t i = 0; i < key_count; ++i)
			set_record(&recs[(*count)++], keys[modifier_count + i], modifiers, ACT_KEY_PRESS);
	if (action != ACT_KEY_PRESS)
		for (size_t i = key_count; i > 0; --i)
			set_record(&recs[(*count)++], keys[modifier_count + i - 1], modifiers, ACT_KEY_RELEASE);
	return 1;
}

static int update_key_holds(const struct keyrec *recs, size_t count, struct key_hold *held, size_t *held_count)
{
	for (size_t i = 0; i < count; ++i) {
		const struct keyrec *rec = &recs[i];
		if (rec->action == ACT_RELEASE_ALL) {
			*held_count = 0;
			continue;
		}
		unsigned key = keysvc_key_index(rec->code_lo | rec->code_hi << 8);
		size_t slot = 0;
		for (; slot < *held_count; ++slot) {
			unsigned held_key = held[slot].rec.code_lo | held[slot].rec.code_hi << 8;
			if (keysvc_key_index(held_key) == key)
				break;
		}
		if (rec->action == ACT_KEY_RELEASE) {
			if (slot < *held_count) {
				memmove(&held[slot], &held[slot + 1], (*held_count - slot - 1) * sizeof *held);
				--*held_count;
			}
		} else {
			if (slot < *held_count || *held_count == KEYSVC_MAX_HELD)
				return 0;
			uint8_t modifiers = rec->modifiers;
			for (size_t j = 0; j < *held_count; ++j) {
				unsigned held_key = held[j].rec.code_lo | held[j].rec.code_hi << 8;
				modifiers |= held[j].rec.modifiers;
				if (keydefs[held_key].plain == 0xAA00)
					modifiers |= MOD_CTRL;
				if (keydefs[held_key].plain == 0xAB00)
					modifiers |= MOD_SHIFT;
			}
			uint16_t code = keysvc_key_code(key, modifiers);
			for (size_t j = 0; j < *held_count; ++j)
				if ((held[j].code >> 8) == (code >> 8))
					return 0;
			if (rec->action == ACT_KEY_PRESS)
				held[(*held_count)++] = (struct key_hold){ *rec, code };
		}
	}
	return 1;
}

static int parse_key_request(int argc, char **argv, struct keyrec *recs, struct key_step *steps)
{
	size_t count = 0, held_count = 0;
	struct key_hold held[KEYSVC_MAX_HELD];
	unsigned char known_absent[KEYSVC_KEY_COUNT] = { 0 };
	unsigned total_wait = 0;
	for (int i = 0; i < argc; ++i) {
		steps[i] = (struct key_step){ count, 0, 0 };
		if (!strncmp(argv[i], "wait:", 5)) {
			const char *digits = argv[i] + 5;
			unsigned milliseconds = 0;
			if (!*digits)
				goto invalid;
			for (; *digits; ++digits) {
				if (*digits < '0' || *digits > '9' || milliseconds > MAX_KEY_WAIT_MS / 10)
					goto invalid;
				milliseconds = milliseconds * 10 + (*digits - '0');
				if (milliseconds > MAX_KEY_WAIT_MS)
					goto invalid;
			}
			if (milliseconds > MAX_KEY_WAIT_MS - total_wait)
				goto invalid;
			total_wait += milliseconds;
			steps[i].wait_ms = milliseconds;
			continue;
		}
		struct keyrec group[KEYSVC_MAX_HELD * 2];
		size_t group_count = 0;
		if (!strcmp(argv[i], "release-all")) {
			set_record(group, 0, 0, ACT_RELEASE_ALL);
			group_count = 1;
		} else if (!parse_key_group(argv[i], group, &group_count)) {
			goto invalid;
		}
		if (group_count > MAX_KEY_RECORDS - count) {
			fprintf(stderr, "at most %d key records per call\n", MAX_KEY_RECORDS);
			return 0;
		}
		for (size_t j = 0; j < group_count; ++j) {
			if (group[j].action == ACT_RELEASE_ALL) {
				memset(known_absent, 1, sizeof known_absent);
				continue;
			}
			unsigned key = keysvc_key_index(group[j].code_lo | group[j].code_hi << 8);
			if (group[j].action == ACT_KEY_RELEASE && known_absent[key])
				goto invalid;
			known_absent[key] = group[j].action != ACT_KEY_PRESS;
		}
		if (!update_key_holds(group, group_count, held, &held_count))
			goto invalid;
		memcpy(&recs[count], group, group_count * sizeof *group);
		steps[i].count = group_count;
		count += group_count;
		continue;
	invalid:
		fprintf(stderr, "invalid key sequence item '%s'\n", argv[i]);
		return 0;
	}
	if (!count) {
		fprintf(stderr, "no keys given\n");
		return 0;
	}
	return 1;
}

// A printable character as the key that produces it. Upper case is the shifted letter.
static int parse_char(char c, struct keyrec *rec)
{
	static const struct { char c; const char *name; } punct[] = {
		{ ' ', "space" }, { '\n', "enter" }, { '\t', "tab" },
		{ '.', "dot" }, { ',', "comma" }, { '(', "pleft" }, { ')', "pright" },
		{ '+', "plus" }, { '-', "minus" }, { '*', "mult" }, { '/', "div" },
		{ '=', "equ" }, { '^', "pow" }, { '<', "lt" }, { '>', "gt" }, { '?', "ques" },
		{ ':', "colon" }, { '"', "quote" }, { '\'', "apos" }, { '|', "bar" },
	};
	char name[2] = { c, 0 };
	const struct keydef *key;

	if (c >= 'A' && c <= 'Z') {
		name[0] = c - 'A' + 'a';
		key = find_key(name);
		if (!key)
			return 0;
		set_record(rec, key - keydefs, MOD_SHIFT, ACT_KEY_TAP);
		return 1;
	}
	if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
		key = find_key(name);
		if (!key)
			return 0;
		set_record(rec, key - keydefs, 0, ACT_KEY_TAP);
		return 1;
	}
	for (size_t i = 0; i < sizeof punct / sizeof punct[0]; i++) {
		if (punct[i].c != c)
			continue;
		key = find_key(punct[i].name);
		if (!key)
			return 0;
		set_record(rec, key - keydefs, 0, ACT_KEY_TAP);
		return 1;
	}
	return 0;
}

static int send_records(nspire_handle_t *handle, const struct keyrec *recs, size_t count)
{
	uint8_t reply[16];
	size_t got = 0;
	int err = nspire_service_exchange(handle, KEYSVC_SID, recs, count * sizeof *recs,
					  reply, sizeof reply, &got);
	if (err) {
		fail("keysvc", err);
	} else if (got < 2 || reply[0] != 'k') {
		fprintf(stderr, "keysvc answered %zu byte(s) that are not its reply\n", got);
	} else {
		printf("keysvc accepted %u of %zu\n", reply[1], count);
		if (reply[1] == count)
			return 0;
	}
	fprintf(stderr, "key state may be unknown. Reconnect, then run nsptool key release-all.\n");
	return 1;
}

static int require_keysvc(nspire_handle_t *handle, int show_status)
{
	const struct keyrec query = { 0, 0, 0, ACT_QUERY };
	uint8_t reply[16];
	size_t got = 0;
	int err = nspire_service_exchange(handle, KEYSVC_SID, &query, sizeof query,
					  reply, sizeof reply, &got);
	if (err)
		return fail("keysvc version query", err);
	if (got != 4 || reply[0] != 'k' || reply[1] != 1) {
		fprintf(stderr, "keysvc version query returned an unrecognized reply (%zu bytes). The version is unknown.\n", got);
		return 1;
	}
	int compatible = reply[2] == KEYSVC_VERSION && reply[3] >= KEYSVC_MAX_HELD;
	if (show_status)
		printf("keysvc version %u\nheld capacity %u\ncompatible %s\n",
			reply[2], reply[3], compatible ? "yes" : "no");
	if (compatible)
		return 0;
	fprintf(stderr, "keysvc reports version %u and capacity %u. Required: version %u and capacity at least %u. Install the current keysvc.tns and restart the calculator.\n",
		reply[2], reply[3], KEYSVC_VERSION, KEYSVC_MAX_HELD);
	return 1;
}
static void interrupt_keys(int signo)
{
	key_interrupted = signo;
}

static int wait_for_keys(unsigned milliseconds)
{
	struct timespec remaining = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };
	while (!key_interrupted && nanosleep(&remaining, &remaining)) {
		if (errno != EINTR) {
			perror("key wait");
			return 1;
		}
	}
	return key_interrupted ? 1 : 0;
}

static int run_key_request(nspire_handle_t *handle, const struct keyrec *recs,
			   const struct key_step *steps, size_t step_count)
{
	if (require_keysvc(handle, 0)) {
		fprintf(stderr, "No keys from this request were dispatched.\n");
		return 1;
	}
	struct sigaction handler = { 0 }, previous_int, previous_term;
	handler.sa_handler = interrupt_keys;
	sigemptyset(&handler.sa_mask);
	key_interrupted = 0;
	if (sigaction(SIGINT, &handler, &previous_int)) {
		perror("key interrupt handler");
		return 1;
	}
	if (sigaction(SIGTERM, &handler, &previous_term)) {
		perror("key termination handler");
		sigaction(SIGINT, &previous_int, NULL);
		return 1;
	}
	struct key_hold held[KEYSVC_MAX_HELD];
	size_t held_count = 0;
	int status = 0;
	for (size_t step = 0; step < step_count && !key_interrupted;) {
		if (!steps[step].count) {
			status = wait_for_keys(steps[step++].wait_ms);
			if (status)
				break;
			continue;
		}
		size_t first = steps[step].first, count = 0;
		do {
			count += steps[step++].count;
		} while (step < step_count && steps[step].count &&
			 steps[step].count <= KEYSVC_MAX_RECORDS - count);
		status = send_records(handle, &recs[first], count);
		if (status)
			break;
		update_key_holds(&recs[first], count, held, &held_count);
	}
	if (key_interrupted)
		status = 128 + key_interrupted;
	if (status) {
		while (held_count) {
			struct keyrec release = held[--held_count].rec;
			release.action = ACT_KEY_RELEASE;
			if (send_records(handle, &release, 1))
				fprintf(stderr, "could not release a key held by this request\n");
		}
	}
	sigaction(SIGTERM, &previous_term, NULL);
	sigaction(SIGINT, &previous_int, NULL);
	return status;
}

static int cmd_restart(nspire_handle_t *handle)
{
	struct keyrec restart;
	set_record(&restart, KEYSVC_RESTART_MAGIC, 0, ACT_RESTART);
	uint8_t reply[16];
	size_t got = 0;
	int err = nspire_service_exchange(handle, KEYSVC_SID, &restart, sizeof restart,
					  reply, sizeof reply, &got);
	if (!err && got == 2 && reply[0] == 'k') {
		if (reply[1] == 1) {
			printf("Restart request accepted (boot completion unverified).\n");
			return 0;
		}
		if (reply[1] == 0) {
			fprintf(stderr, "restart is unsupported by the resident keysvc helper or this hardware\n");
			return 1;
		}
	}
	if (err)
		fail("restart", err);
	fprintf(stderr, "restart is unconfirmed. Check the calculator before retrying.\n");
	return 1;
}

static int cmd_key(nspire_handle_t *handle, int argc, char **argv)
{
	if (argc <= 0) {
		fprintf(stderr, "no keys given\n");
		return 2;
	}
	if (argc == 1 && !strcmp(argv[0], "restart"))
		return cmd_restart(handle);
	struct keyrec recs[MAX_KEY_RECORDS];
	struct key_step *steps = calloc(argc, sizeof *steps);
	if (!steps) {
		perror("key sequence");
		return 1;
	}
	int status = parse_key_request(argc, argv, recs, steps) ?
		run_key_request(handle, recs, steps, argc) : 2;
	free(steps);
	return status;
}

static int cmd_type(nspire_handle_t *handle, const char *text)
{
	struct keyrec recs[64];
	size_t count = 0;

	for (const char *p = text; *p; p++) {
		if (count == sizeof recs / sizeof recs[0]) {
			fprintf(stderr, "at most %zu characters per call\n", count);
			return 2;
		}
		if (!parse_char(*p, &recs[count])) {
			fprintf(stderr, "no key for character '%c'\n", *p);
			return 2;
		}
		count++;
	}
	if (!count) {
		fprintf(stderr, "nothing to type\n");
		return 2;
	}
	const struct key_step step = { 0, count, 0 };
	return run_key_request(handle, recs, &step, 1);
}

static void list_keys(void)
{
	for (size_t i = 0; i < sizeof keydefs / sizeof keydefs[0]; i++)
		printf("%-7s%c", keydefs[i].name, (i % 8 == 7) ? '\n' : ' ');
	printf("\n");
}

static uint32_t find_os_key(const char *name)
{
	static const struct { const char *name, *canonical; } aliases[] = {
		{ "ret", "new-line" }, { "del", "back-space" }, { "backspace", "back-space" },
		{ "cat", "catalog" }, { "bar", "such-that" }, { "apos", "apostrophe" },
		{ "div", "slash" }, { "mult", "times" }, { "equ", "equal" },
		{ "pow", "power" }, { "pleft", "left-parentheses" }, { "pright", "right-parentheses" },
		{ "dot", "point" }, { "lt", "less-than" }, { "gt", "greater-than" },
		{ "ques", "question-mark" }, { "ee", "exp" }, { "squ", "square" },
		{ "pow10", "ten-power" }, { "0", "zero" }, { "1", "one" }, { "2", "two" },
		{ "3", "three" }, { "4", "four" }, { "5", "five" }, { "6", "six" },
		{ "7", "seven" }, { "8", "eight" }, { "9", "nine" },
	};
	size_t prefix = !strncmp(name, "ctrl+", 5) ? 5 : !strncmp(name, "shift+", 6) ? 6 : 0;
	char canonical[80];
	if (strlen(name) >= sizeof canonical)
		return 0;
	strcpy(canonical, name);
	for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; ++i)
		if (!strcmp(name + prefix, aliases[i].name)) {
			strcpy(canonical + prefix, aliases[i].canonical);
			break;
		}
	for (size_t i = 0; i < sizeof os_keydefs / sizeof os_keydefs[0]; ++i)
		if (!strcmp(canonical, os_keydefs[i].name))
			return os_keydefs[i].code;
	return 0;
}

static uint32_t os_key_for_character(unsigned char character)
{
	if (character == '\n')
		return KEYNSP_ENTER;
	if (character == '\t')
		return KEYNSP_TAB;
	if (character < 32 || character > 126)
		return 0;
	for (size_t i = 0; i < sizeof os_keydefs / sizeof os_keydefs[0]; ++i) {
		const char *name = os_keydefs[i].name;
		// Shift+Tab carries a vertical-bar byte but remains a navigation chord.
		if (strchr(name, '+') &&
		    !(character >= 'A' && character <= 'Z' && !strncmp(name, "shift+", 6) && strlen(name) == 7) &&
		    strcmp(name, "ctrl+colon") && strcmp(name, "ctrl+apostrophe"))
			continue;
		if ((os_keydefs[i].code >> 16) == character)
			return os_keydefs[i].code;
	}
	return 0;
}

static int parse_os_request(int is_text, int argc, char **argv, uint32_t *keys, size_t *count)
{
	if (argc < 1 || (is_text && argc != 1)) {
		fprintf(stderr, "use key-os <key>... or type-os <text>\n");
		return 0;
	}
	*count = is_text ? strlen(argv[0]) : (size_t)argc;
	if (!*count || *count > MAX_KEY_RECORDS) {
		fprintf(stderr, "standard keys require 1 to %d keys per call\n", MAX_KEY_RECORDS);
		return 0;
	}
	for (size_t i = 0; i < *count; ++i) {
		keys[i] = is_text ? os_key_for_character((unsigned char)argv[0][i]) : find_os_key(argv[i]);
		if (!keys[i]) {
			if (is_text)
				fprintf(stderr, "unsupported standard-key character at byte %zu (0x%02x)\n", i + 1,
					(unsigned char)argv[0][i]);
			else
				fprintf(stderr, "unsupported standard key: %s. Use key-os --list.\n", argv[i]);
			return 0;
		}
	}
	return 1;
}

static int send_os_keys(nspire_handle_t *handle, const uint32_t *keys, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		int status = nspire_send_key(handle, keys[i]);
		if (status) {
			fail("standard key", status);
			fprintf(stderr, "key %zu of %zu is unconfirmed. Check the calculator before retrying.\n", i + 1, count);
			return 1;
		}
	}
	printf("standard keys: transport acknowledged %zu of %zu, visible effect unverified\n", count, count);
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"nsptool <command>\n"
		"  info                     device name, versions, storage, battery, lcd\n"
		"  ls [path]                list a directory, default /\n"
		"  get <remote> <local>     copy a file off the calculator\n"
		"  put <local> <remote>     copy a file onto the calculator\n"
		"  rm <remote>              delete a file\n"
		"  mkdir <remote>           create a directory\n"
		"  rmdir <remote>           remove an empty directory\n"
		"  screenshot <local.ppm>   capture the screen\n"
		"  restart                  hard restart, losing unsaved work\n"
		"  keysvc-status            query helper version and capacity without sending keys\n"
		"  key <item>...            queue keys and chords: down, ctrl+shift+a, a+b\n"
		"                           +a+b holds, -a+b releases, wait:250 waits, release-all\n"
		"  type <text>              press the keys that type text\n"
		"  key-os <key>...          standard OS taps, without keysvc. Use --list for names\n"
		"  type-os <text>           standard OS taps for supported ASCII text\n"
		"  keys                     list the key names\n"
		"  session                  serve framed requests on stdin and stdout\n"
		"  session-version          print the supported session protocol version\n");
}

struct connection {
	nspire_handle_t *handle;
	int lock_fd;
};

static void disconnect_device(struct connection *connection)
{
	if (connection->handle)
		nspire_free(connection->handle);
	connection->handle = NULL;
	if (connection->lock_fd >= 0)
		close(connection->lock_fd);
	connection->lock_fd = -1;
}

static int lock_device(void)
{
	uid_t uid = getuid();
	if (uid == 0) {
		const char *sudo_uid = getenv("SUDO_UID");
		char *end;
		errno = 0;
		uintmax_t value = sudo_uid ? strtoumax(sudo_uid, &end, 10) : 0;
		if (!sudo_uid || *sudo_uid < '0' || *sudo_uid > '9' || errno ||
		    *end || !value || value != (uid_t)value) {
			fprintf(stderr, "USB ownership requires a normal user or a valid SUDO_UID\n");
			return -1;
		}
		uid = (uid_t)value;
	}
	char path[96];
	snprintf(path, sizeof path, "/tmp/nsptool-%ju.lock", (uintmax_t)uid);
	int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd >= 0) {
		if (geteuid() != uid && fchown(fd, uid, -1)) {
			perror("USB ownership lock owner");
			close(fd);
			return -1;
		}
	} else if (errno == EEXIST) {
		fd = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
	}
	if (fd < 0) {
		perror("USB ownership lock");
		return -1;
	}
	struct stat attributes;
	if (fstat(fd, &attributes) || !S_ISREG(attributes.st_mode) ||
	    attributes.st_uid != uid || attributes.st_nlink != 1 ||
	    (attributes.st_mode & 0777) != 0600) {
		fprintf(stderr, "refusing unsafe USB ownership lock: %s\n", path);
		close(fd);
		return -1;
	}
	if (flock(fd, LOCK_EX | LOCK_NB)) {
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			char owner[32] = { 0 };
			ssize_t length = pread(fd, owner, sizeof owner - 1, 0);
			if (length <= 0 || strspn(owner, "0123456789") != (size_t)length)
				strcpy(owner, "unknown");
			fprintf(stderr, "USB busy: nsptool process %s owns the calculator (%s)\n", owner, path);
		} else {
			perror("USB ownership lock");
		}
		close(fd);
		return -1;
	}
	char owner[32];
	int length = snprintf(owner, sizeof owner, "%jd", (intmax_t)getpid());
	if (ftruncate(fd, 0) || pwrite(fd, owner, length, 0) != length) {
		perror("USB ownership lock record");
		close(fd);
		return -1;
	}
	return fd;
}

static int connect_device(struct connection *connection)
{
	if (connection->handle)
		return 0;
	connection->lock_fd = lock_device();
	if (connection->lock_fd < 0)
		return 1;
	int err = nspire_init(&connection->handle);
	if (err) {
		fprintf(stderr, "connect: %s\n", nspire_strerror(err));
		fprintf(stderr, "Is the calculator plugged in and out of any transfer dialog?\n");
		disconnect_device(connection);
		return 1;
	}
	if (drop_privileges()) {
		disconnect_device(connection);
		return 1;
	}
	return 0;
}

static int dispatch_command(struct connection *connection, int argc, char **argv)
{
	if (argc < 1) {
		usage();
		return 2;
	}

	const char *cmd = argv[0];
	if (!strcmp(cmd, "session-version") && argc == 1) {
		puts("1");
		return 0;
	}
	if (!strcmp(cmd, "keys")) {
		list_keys();
		return 0;
	}
	int is_os_key = !strcmp(cmd, "key-os"), is_os_text = !strcmp(cmd, "type-os");
	if (is_os_key && argc == 2 && !strcmp(argv[1], "--list")) {
		for (size_t i = 0; i < sizeof os_keydefs / sizeof os_keydefs[0]; ++i)
			puts(os_keydefs[i].name);
		return 0;
	}
	uint32_t os_keys[MAX_KEY_RECORDS];
	size_t os_count = 0;
	if ((is_os_key || is_os_text) && !parse_os_request(is_os_text, argc - 1, argv + 1, os_keys, &os_count))
		return 2;

	if (connect_device(connection))
		return 1;
	nspire_handle_t *handle = connection->handle;

	int status;
	if (is_os_key || is_os_text)
		status = send_os_keys(handle, os_keys, os_count);
	else if (!strcmp(cmd, "info") && argc == 1)
		status = cmd_info(handle);
	else if (!strcmp(cmd, "keysvc-status") && argc == 1)
		status = require_keysvc(handle, 1);
	else if (!strcmp(cmd, "ls"))
		status = cmd_ls(handle, argc > 1 ? argv[1] : "/");
	else if (!strcmp(cmd, "get") && argc == 3)
		status = cmd_get(handle, argv[1], argv[2]);
	else if (!strcmp(cmd, "put") && argc == 3)
		status = cmd_put(handle, argv[1], argv[2]);
	else if (!strcmp(cmd, "rm") && argc == 2)
		status = cmd_rm(handle, argv[1]);
	else if (!strcmp(cmd, "mkdir") && argc == 2)
		status = cmd_mkdir(handle, argv[1]);
	else if (!strcmp(cmd, "rmdir") && argc == 2)
		status = cmd_rmdir(handle, argv[1]);
	else if (!strcmp(cmd, "screenshot") && argc == 2)
		status = cmd_screenshot(handle, argv[1]);
	else if (!strcmp(cmd, "restart") && argc == 1)
		status = cmd_restart(handle);
	else if (!strcmp(cmd, "key") && argc >= 2)
		status = cmd_key(handle, argc - 1, argv + 1);
	else if (!strcmp(cmd, "type") && argc == 2)
		status = cmd_type(handle, argv[1]);
	else {
		usage();
		status = 2;
	}

	if (status)
		disconnect_device(connection);
	return status;
}

#define SESSION_MAX_FRAME (1024 * 1024)
#define SESSION_MAX_ARGS 8192
#define SESSION_MAX_OUTPUT (8 * 1024 * 1024)

static int read_frame_bytes(void *buffer, size_t length)
{
	size_t offset = 0;
	while (offset < length) {
		ssize_t got = read(STDIN_FILENO, (unsigned char *)buffer + offset, length - offset);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return got == 0 && offset == 0 ? 0 : -1;
		offset += got;
	}
	return 1;
}

static uint32_t frame_word(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
	       (uint32_t)bytes[2] << 8 | bytes[3];
}

static void free_arguments(char **argv, uint32_t argc)
{
	for (uint32_t i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}

static int valid_utf8(const unsigned char *bytes, size_t length)
{
	for (size_t i = 0; i < length;) {
		uint32_t code = bytes[i++];
		if (code < 0x80)
			continue;
		unsigned trailing;
		uint32_t minimum;
		if (code >= 0xC2 && code <= 0xDF) {
			trailing = 1;
			minimum = 0x80;
			code &= 0x1F;
		} else if (code >= 0xE0 && code <= 0xEF) {
			trailing = 2;
			minimum = 0x800;
			code &= 0x0F;
		} else if (code >= 0xF0 && code <= 0xF4) {
			trailing = 3;
			minimum = 0x10000;
			code &= 7;
		} else {
			return 0;
		}
		if (trailing > length - i)
			return 0;
		while (trailing--) {
			if ((bytes[i] & 0xC0) != 0x80)
				return 0;
			code = code << 6 | (bytes[i++] & 0x3F);
		}
		if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
			return 0;
	}
	return 1;
}

static int read_request(char ***arguments, int *count)
{
	unsigned char header[4];
	int got = read_frame_bytes(header, sizeof header);
	if (got != 1)
		return got;
	uint32_t length = frame_word(header);
	if (length < 4 || length > SESSION_MAX_FRAME)
		return -1;
	unsigned char *frame = malloc(length);
	if (!frame)
		return -1;
	if (read_frame_bytes(frame, length) != 1) {
		free(frame);
		return -1;
	}
	uint32_t argc = frame_word(frame);
	if (!argc || argc > SESSION_MAX_ARGS) {
		free(frame);
		return -1;
	}
	char **argv = calloc(argc + 1, sizeof *argv);
	if (!argv) {
		free(frame);
		return -1;
	}
	size_t offset = 4;
	for (uint32_t i = 0; i < argc; i++) {
		if (length - offset < 4)
			goto malformed;
		uint32_t bytes = frame_word(frame + offset);
		offset += 4;
		if (bytes > length - offset || memchr(frame + offset, 0, bytes) ||
		    !valid_utf8(frame + offset, bytes))
			goto malformed;
		argv[i] = malloc((size_t)bytes + 1);
		if (!argv[i])
			goto malformed;
		memcpy(argv[i], frame + offset, bytes);
		argv[i][bytes] = 0;
		offset += bytes;
	}
	if (offset != length)
		goto malformed;
	free(frame);
	*arguments = argv;
	*count = argc;
	return 1;

malformed:
	free_arguments(argv, argc);
	free(frame);
	return -1;
}

static int write_frame_bytes(int fd, const void *buffer, size_t length)
{
	size_t offset = 0;
	while (offset < length) {
		ssize_t sent = write(fd, (const unsigned char *)buffer + offset, length - offset);
		if (sent < 0 && errno == EINTR)
			continue;
		if (sent <= 0)
			return -1;
		offset += sent;
	}
	return 0;
}

static int write_response(int fd, int status, const char *output, size_t length)
{
	unsigned char header[8];
	for (unsigned i = 0; i < 4; i++) {
		header[i] = (uint32_t)status >> (24 - 8 * i);
		header[i + 4] = (uint32_t)length >> (24 - 8 * i);
	}
	return write_frame_bytes(fd, header, sizeof header) || write_frame_bytes(fd, output, length);
}

struct command_output {
	int fd;
	char *bytes;
	size_t length;
	int failed;
};

static void *read_command_output(void *context)
{
	struct command_output *output = context;
	char chunk[8192];
	for (;;) {
		ssize_t got = read(output->fd, chunk, sizeof chunk);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			if (got < 0)
				output->failed = 1;
			break;
		}
		size_t keep = got;
		if (keep > SESSION_MAX_OUTPUT - output->length) {
			keep = SESSION_MAX_OUTPUT - output->length;
			output->failed = 1;
		}
		memcpy(output->bytes + output->length, chunk, keep);
		output->length += keep;
	}
	close(output->fd);
	return NULL;
}

static int capture_command(struct connection *connection, int argc, char **argv,
			   int sink, struct command_output *output)
{
	int channels[2];
	pthread_t reader;
	output->bytes = malloc(SESSION_MAX_OUTPUT);
	if (!output->bytes || pipe(channels))
		return -1;
	output->fd = channels[0];
	if (pthread_create(&reader, NULL, read_command_output, output)) {
		close(channels[0]);
		close(channels[1]);
		return -1;
	}
	int status = -1;
	fflush(stdout);
	fflush(stderr);
	if (dup2(channels[1], STDOUT_FILENO) >= 0 && dup2(channels[1], STDERR_FILENO) >= 0) {
		close(channels[1]);
		channels[1] = -1;
		status = dispatch_command(connection, argc, argv);
		if (fflush(stdout) || fflush(stderr))
			status = -1;
	}
	if (dup2(sink, STDOUT_FILENO) < 0 || dup2(sink, STDERR_FILENO) < 0)
		_exit(1);
	if (channels[1] >= 0)
		close(channels[1]);
	pthread_join(reader, NULL);
	if (output->failed)
		status = -1;
	if (status)
		disconnect_device(connection);
	return status;
}

static int run_session(void)
{
	int protocol = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
	int sink = open("/dev/null", O_WRONLY | O_CLOEXEC);
	if (protocol < 0 || sink < 0) {
		perror("session output");
		if (protocol >= 0)
			close(protocol);
		if (sink >= 0)
			close(sink);
		return 1;
	}
	fflush(stdout);
	fflush(stderr);
	if (dup2(sink, STDOUT_FILENO) < 0 || dup2(sink, STDERR_FILENO) < 0)
		_exit(1);
	signal(SIGPIPE, SIG_IGN);
	struct connection connection = { NULL, -1 };
	int status = 0;
	for (;;) {
		char **argv;
		int argc;
		int got = read_request(&argv, &argc);
		if (!got)
			break;
		if (got < 0) {
			const char *message = "invalid or incomplete session request\n";
			write_response(protocol, 2, message, strlen(message));
			status = 2;
			break;
		}
		struct command_output output = { 0 };
		int command_status = capture_command(&connection, argc, argv, sink, &output);
		free_arguments(argv, argc);
		const char *message = command_status < 0 ? "session output capture failed or exceeded 8 MiB. Command may have applied.\n" : output.bytes;
		size_t length = command_status < 0 ? strlen(message) : output.length;
		int failed = write_response(protocol, command_status < 0 ? 1 : command_status, message, length);
		free(output.bytes);
		if (failed || command_status < 0 || command_status >= 128) {
			status = command_status >= 128 ? command_status : 1;
			break;
		}
	}
	disconnect_device(&connection);
	close(sink);
	close(protocol);
	return status;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "session"))
		return run_session();
	struct connection connection = { NULL, -1 };
	int status = dispatch_command(&connection, argc - 1, argv + 1);
	disconnect_device(&connection);
	return status;
}
