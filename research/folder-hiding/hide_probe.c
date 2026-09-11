#include <os.h>

#ifndef MODE
#define MODE 0
#endif

static const char *visible[] = {
	"/documents/HideLab/Dot.tns",
	"/documents/HideLab/Extension.tns",
	"/documents/HideLab/Outside.tns",
	"/documents/HideLab/Folder",
	"/documents/HideLab/LockedFolder"
};
static const char *concealed[] = {
	"/documents/HideLab/.Dot.tns",
	"/documents/HideLab/Extension.bin",
	"/hide-research-store/Outside.tns",
	"/documents/HideLab/.Folder",
	"/hide-research-store/LockedFolder"
};

static int copy_fixture(const char *destination)
{
	struct stat st;
	if (stat(destination, &st) == 0)
		return 0;
	FILE *input = fopen("/documents/HideLab/Proof.tns", "rb");
	if (!input)
		return -1;
	FILE *output = fopen(destination, "wb");
	if (!output) {
		fclose(input);
		return -1;
	}
	unsigned char block[1024];
	size_t count;
	int failed = 0;
	while ((count = fread(block, 1, sizeof block, input)) != 0)
		if (fwrite(block, 1, count, output) != count) {
			failed = 1;
			break;
		}
	failed |= ferror(input) != 0;
	failed |= fclose(output) != 0;
	fclose(input);
	return failed ? -1 : 0;
}

static void record_state(FILE *log)
{
	struct stat st;
	for (unsigned i = 0; i < 5; i++) {
		int rc = stat(visible[i], &st);
		fprintf(log, "visible %u exists=%d size=%lu path=%s\n", i, rc == 0,
			rc == 0 ? (unsigned long)st.st_size : 0, visible[i]);
		rc = stat(concealed[i], &st);
		fprintf(log, "concealed %u exists=%d size=%lu path=%s\n", i, rc == 0,
			rc == 0 ? (unsigned long)st.st_size : 0, concealed[i]);
	}
}

static int move_fixtures(FILE *log, int restore)
{
	struct stat st;
	for (unsigned i = 0; i < 5; i++) {
		const char *source = restore ? concealed[i] : visible[i];
		const char *destination = restore ? visible[i] : concealed[i];
		if (stat(source, &st) != 0 || stat(destination, &st) == 0) {
			fprintf(log, "move %u refused, source missing or destination exists\n", i);
			return -1;
		}
	}
	int failed = 0;
	for (unsigned i = 0; i < 5; i++) {
		const char *source = restore ? concealed[i] : visible[i];
		const char *destination = restore ? visible[i] : concealed[i];
		int rc = rename(source, destination);
		fprintf(log, "move %u rc=%d errno=%d\n", i, rc, errno);
		failed |= rc != 0;
	}
	return failed ? -1 : 0;
}

int main(void)
{
	if (nl_isstartup())
		return 1;
	char *code = NULL;
	int length = -1;
	if (MODE == 2)
		length = show_msg_user_input("Restore hidden files", "Enter test code", "", &code);
	FILE *log = fopen("/documents/HideResearch.log.tns", "w");
	if (!log) {
		free(code);
		return 1;
	}
	fprintf(log, "mode=%d documents=%s hwsubtype=%u\n", MODE, get_documents_dir(), nl_hwsubtype());
	if (MODE == 2)
		fprintf(log, "input length=%d text=%s\n", length, code ? code : "(null)");
	int rc = 0;
	if (MODE == 0) {
		fprintf(log, "mkdir store=%d\n", mkdir("/hide-research-store", 0700));
		fprintf(log, "mkdir folder=%d\n", mkdir(visible[3], 0700));
		fprintf(log, "mkdir locked folder=%d\n", mkdir(visible[4], 0700));
		fprintf(log, "mkdir nested=%d\n", mkdir("/documents/HideLab/LockedFolder/Nested", 0700));
		for (unsigned i = 0; i < 3; i++)
			rc |= copy_fixture(visible[i]);
		rc |= copy_fixture("/documents/HideLab/Folder/Inside.tns");
		rc |= copy_fixture("/documents/HideLab/LockedFolder/Inside.tns");
		rc |= copy_fixture("/documents/HideLab/LockedFolder/Nested/Deep.tns");
	} else if (MODE == 1) {
		rc = move_fixtures(log, 0);
	} else if (MODE == 2) {
		if (length == 4 && code && !strcmp(code, "4931")) {
			fprintf(log, "code accepted\n");
			rc = move_fixtures(log, 1);
		} else {
			fprintf(log, "code rejected or cancelled\n");
		}
	}
	fprintf(log, "operation rc=%d\n", rc);
	record_state(log);
	free(code);
	fprintf(log, "before home refresh\n");
	fflush(log);
	refresh_homescr();
	fprintf(log, "before browser refresh\n");
	fflush(log);
	refresh_docbrowser(0x1B5A);
	fprintf(log, "after browser refresh\n");
	fclose(log);
	return rc;
}
