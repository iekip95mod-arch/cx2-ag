// Finds every copy of the unified module in the documents tree. nrequire matches a module by
// basename across the whole tree, so a stale copy anywhere shadows the deployed one and the symptom
// is a binding that looks like it was never built. See BUDGETS.md.
//
// Listing only. Deleting is a separate deliberate step, not a side effect of looking.

#include <dirent.h>
#include <os.h>
#include <stdarg.h>
#include <string.h>

#define RESULT_PATH "/documents/nps_walk.txt.tns"
#define WANTED "nps_nspire.luax.tns"
#define PATH_MAX_LEN 512
#define DEPTH_MAX 8

static FILE *result;

static void report(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (result) {
		va_start(ap, fmt);
		vfprintf(result, fmt, ap);
		va_end(ap);
	}
}

static long file_size(const char *path)
{
	FILE *f = fopen(path, "rb");
	long size;

	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fclose(f);
	return size;
}

// nuc_dirent carries no d_type, so the only way to tell a directory apart is to try opening it as one.
static int is_directory(const char *path)
{
	DIR *dir = opendir(path);

	if (!dir)
		return 0;
	closedir(dir);
	return 1;
}

static int matches_wanted(const char *name)
{
	return strcmp(name, WANTED) == 0;
}

static int ends_with_luax(const char *name)
{
	size_t length = strlen(name);
	const char *suffix = ".luax.tns";
	size_t suffix_length = strlen(suffix);

	return length >= suffix_length && strcmp(name + length - suffix_length, suffix) == 0;
}

static unsigned found;
static unsigned visited;

static void walk(char *path, size_t length, unsigned depth)
{
	DIR *dir = opendir(path);
	struct dirent *entry;

	if (!dir) {
		report("unreadable %s\n", path);
		return;
	}

	while ((entry = readdir(dir)) != NULL) {
		size_t name_length = strlen(entry->d_name);
		size_t next;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (length + 1 + name_length + 1 > PATH_MAX_LEN) {
			report("too long under %s: %s\n", path, entry->d_name);
			continue;
		}

		path[length] = '/';
		memcpy(path + length + 1, entry->d_name, name_length + 1);
		next = length + 1 + name_length;
		visited++;

		if (is_directory(path)) {
			if (depth + 1 < DEPTH_MAX)
				walk(path, next, depth + 1);
			else
				report("depth stop %s\n", path);
		} else if (matches_wanted(entry->d_name)) {
			found++;
			report("MATCH %s %ld\n", path, file_size(path));
		} else if (ends_with_luax(entry->d_name)) {
			report("other %s %ld\n", path, file_size(path));
		}

		path[length] = 0;
	}

	closedir(dir);
}

int main(void)
{
	char path[PATH_MAX_LEN];

	result = fopen(RESULT_PATH, "w");
	strcpy(path, "/documents");

	report("schema = nps-module-walk-v1\n");
	report("wanted = %s\n", WANTED);
	walk(path, strlen(path), 0);
	report("entries_visited = %u\n", visited);
	report("matches = %u\n", found);
	report("done\n");

	if (result)
		fclose(result);
	return 0;
}
