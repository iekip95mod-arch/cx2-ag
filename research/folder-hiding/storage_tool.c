#include <os.h>
#include "storage.h"

#ifndef ACTION
#define ACTION 0
#endif

static int directory(const char *name)
{
	struct stat st;
	if (stat(name, &st) == 0)
		return S_ISDIR(st.st_mode) ? 0 : -1;
	return mkdir(name, 0700);
}

int main(void)
{
	if (nl_isstartup())
		return 1;
	if (directory("/appdata") || directory("/appdata/test_app") ||
		directory("/appdata/test_app/store"))
		return 1;
	FILE *log = fopen(ACTION == 0 ? "/documents/InventoryReport.tns" : "/appdata/test_app/actions.log", "a");
	if (!log)
		return 1;
	char message[256] = "";
	int rc;
	if (ACTION == 1)
		rc = storage_hide(get_documents_dir(), "/appdata/test_app/store", message, sizeof message);
	else if (ACTION == 2)
		rc = storage_restore(get_documents_dir(), "/appdata/test_app/store", message, sizeof message);
	else
		rc = storage_inventory(get_documents_dir(), log);
	fprintf(log, "action=%d rc=%d message=%s\n", ACTION, rc, message);
	fclose(log);
	refresh_osscr();
	return rc != 0;
}
