#include <os.h>

#ifndef ARM_BOOT_RESTART
#define ARM_BOOT_RESTART 0
#endif

#if ARM_BOOT_RESTART && !defined(ARM_BOOT_RESET)
#define ARM_BOOT_RESET() (*(volatile unsigned *)0x90140020 = 0x80)
#endif

static int exists(const char *name)
{
	struct stat st;
	return stat(name, &st) == 0;
}

static int regular_file(const char *name)
{
	struct stat st;
	return stat(name, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static int copy_new(const char *source, const char *destination)
{
	if (exists(destination))
		return -1;
	FILE *input = fopen(source, "rb");
	if (!input)
		return -1;
	FILE *output = fopen(destination, "wb");
	if (!output) {
		fclose(input);
		return -1;
	}
	static unsigned char bytes[1024];
	size_t count;
	int failed = 0;
	while ((count = fread(bytes, 1, sizeof bytes, input)) != 0)
		if (fwrite(bytes, 1, count, output) != count) {
			failed = 1;
			break;
		}
	failed |= ferror(input) != 0;
	failed |= fclose(input) != 0;
	failed |= fclose(output) != 0;
	return failed ? -1 : 0;
}

int main(void)
{
	if (nl_isstartup() || exists("/documents/ndl") ||
		!regular_file("/appdata/ndl/ndl_resources.tns") ||
		!regular_file("/appdata/ndl/persistent.tns") ||
		#if ARM_BOOT_RESTART
		!is_cx2 ||
		exists("/phoenix/syst/poweroff/currentdoc.tns") ||
		exists("/phoenix/syst/poweroff/currentdoc.data") ||
		!regular_file("/appdata/currentdoc.stock.tns") ||
		!regular_file("/appdata/currentdoc.stock.data") ||
		exists("/appdata/currentdoc.next.tns") ||
		#else
		exists("/appdata/currentdoc.stock.tns") ||
		exists("/appdata/currentdoc.stock.data") ||
		#endif
		exists("/appdata/currentdoc.next.data"))
		return 1;
	FILE *log = fopen("/documents/RelocationStatus.tns", "a");
	if (!log)
		return 1;
	const char *old[] = {
		"/phoenix/syst/poweroff/currentdoc.tns",
		"/phoenix/syst/poweroff/currentdoc.data"
	};
	#if !ARM_BOOT_RESTART
	for (unsigned i = 0; i < 2; i++) {
		FILE *existing = fopen(old[i], "rb");
		unsigned char header[16];
		if (!existing) {
			fclose(log);
			return 1;
		}
		size_t count = fread(header, 1, sizeof header, existing);
		fprintf(log, "original %s first bytes", old[i]);
		for (size_t j = 0; j < count; j++)
			fprintf(log, " %02x", header[j]);
		fputc('\n', log);
		int failed = ferror(existing) || count != sizeof header;
		failed |= fclose(existing) != 0;
		if (failed) {
			fclose(log);
			return 1;
		}
	}
	#endif
	int rc = copy_new("/appdata/ndl/persistent.tns", "/appdata/currentdoc.next.tns");
	if (!rc) {
		uint32_t state[0x21c / sizeof(uint32_t)] = {0};
		state[0] = state[2] = state[4] = 1;
		FILE *output = fopen("/appdata/currentdoc.next.data", "wb");
		if (!output)
			rc = -1;
		else {
			rc = fwrite(state, 1, sizeof state, output) == sizeof state ? 0 : -1;
			if (fclose(output))
				rc = -1;
		}
	}
	const char *source[] = {
		#if !ARM_BOOT_RESTART
		old[0], old[1],
		#endif
		"/appdata/currentdoc.next.tns", "/appdata/currentdoc.next.data"
	};
	const char *destination[] = {
		#if !ARM_BOOT_RESTART
		"/appdata/currentdoc.stock.tns", "/appdata/currentdoc.stock.data",
		#endif
		old[0], old[1]
	};
	unsigned moved = 0;
	if (!rc)
		for (; moved < sizeof source / sizeof source[0]; moved++) {
			if (exists(destination[moved]) || rename(source[moved], destination[moved])) {
				rc = -1;
				break;
			}
			fprintf(log, "boot move %u completed\n", moved);
			fflush(log);
		}
	if (rc)
		while (moved) {
			moved--;
			if (exists(source[moved]) || rename(destination[moved], source[moved])) {
				fprintf(log, "boot rollback %u failed\n", moved);
				break;
			}
		}
	fprintf(log, "arm boot rc=%d\n", rc);
	#if ARM_BOOT_RESTART
	int log_failed = ferror(log) != 0;
	log_failed |= fclose(log) != 0;
	if (log_failed)
		return 1;
	if (!rc)
		ARM_BOOT_RESET();
	#else
	fclose(log);
	#endif
	return rc != 0;
}
