#include <os.h>

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

int main(void)
{
	if (nl_isstartup())
		return 1;
	if (exists("/appdata/ndl") ||
		!regular_file("/documents/ndl/ndl_resources.tns") ||
		!regular_file("/documents/ndl/persistent.tns") ||
		!regular_file("/documents/ndl_resources_custom.tns") ||
		!regular_file("/documents/persistent_custom.tns") ||
		!regular_file("/documents/calc_helpers.luax.tns"))
		return 1;
	if (!exists("/appdata") && mkdir("/appdata", 0700))
		return 1;
	FILE *log = fopen("/appdata/relocation.log", "a");
	if (!log)
		return 1;
	static const char *source[] = {
		"/documents/ndl",
		"/appdata/ndl/ndl_resources.tns",
		"/documents/ndl_resources_custom.tns",
		"/documents/calc_helpers.luax.tns",
		"/appdata/ndl/persistent.tns",
		"/documents/persistent_custom.tns"
	};
	static const char *destination[] = {
		"/appdata/ndl",
		"/appdata/ndl/ndl_resources.stock.tns",
		"/appdata/ndl/ndl_resources.tns",
		"/appdata/ndl/calc_helpers.luax.tns",
		"/appdata/ndl/persistent.stock.tns",
		"/appdata/ndl/persistent.tns"
	};
	unsigned moved = 0;
	int rc = 0;
	for (; moved < sizeof source / sizeof source[0]; moved++) {
		if (exists(destination[moved]) || rename(source[moved], destination[moved])) {
			rc = -1;
			break;
		}
		fprintf(log, "move %u completed\n", moved);
		fflush(log);
	}
	if (rc)
		while (moved) {
			moved--;
			if (exists(source[moved]) || rename(destination[moved], source[moved])) {
				fprintf(log, "rollback %u failed\n", moved);
				break;
			}
			fprintf(log, "rollback %u completed\n", moved);
		}
	fprintf(log, "relocation rc=%d\n", rc);
	fclose(log);
	refresh_osscr();
	return rc != 0;
}
