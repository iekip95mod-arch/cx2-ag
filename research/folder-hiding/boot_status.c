#include <os.h>
#include <errno.h>
#include <zlib.h>

static void report_file(FILE *report, const char *path)
{
	struct stat st;
	if (stat(path, &st)) {
		fprintf(report, "%s stat errno=%d\n", path, errno);
		return;
	}
	if (!S_ISREG(st.st_mode)) {
		fprintf(report, "%s not a regular file\n", path);
		return;
	}
	FILE *input = fopen(path, "rb");
	if (!input) {
		fprintf(report, "%s open errno=%d\n", path, errno);
		return;
	}
	unsigned char bytes[1024];
	unsigned char header[20];
	size_t header_size = 0;
	size_t total = 0;
	uLong crc = crc32(0L, Z_NULL, 0);
	size_t count;
	while ((count = fread(bytes, 1, sizeof bytes, input)) != 0) {
		if (!total) {
			header_size = count < sizeof header ? count : sizeof header;
			memcpy(header, bytes, header_size);
		}
		crc = crc32(crc, bytes, count);
		total += count;
	}
	int failed = ferror(input) != 0;
	failed |= fclose(input) != 0;
	fprintf(report, "%s size=%lu read=%lu crc32=%08lx failed=%d\n",
		path, (unsigned long)st.st_size, (unsigned long)total, crc, failed);
	if (header_size == sizeof header) {
		uint32_t words[5];
		memcpy(words, header, sizeof words);
		fprintf(report, "words=%08lx %08lx %08lx %08lx %08lx\n",
			(unsigned long)words[0], (unsigned long)words[1], (unsigned long)words[2],
			(unsigned long)words[3], (unsigned long)words[4]);
	}
}

int main(void)
{
	if (nl_isstartup())
		return 1;
	FILE *report = fopen("/documents/BootStatusReport.tns", "a");
	if (!report)
		return 1;
	fprintf(report, "boot status begin ndl=%u vector=%08lx\n", nl_ndl_rev(),
		(unsigned long)*(volatile uint32_t *)0x10000020);
	static const char *paths[] = {
		"/phoenix/syst/poweroff/currentdoc.tns",
		"/phoenix/syst/poweroff/currentdoc.data",
		"/appdata/ndl/persistent.tns",
		"/appdata/currentdoc.stock.tns",
		"/appdata/currentdoc.stock.data"
	};
	for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; i++)
		report_file(report, paths[i]);
	return fclose(report) != 0;
}
