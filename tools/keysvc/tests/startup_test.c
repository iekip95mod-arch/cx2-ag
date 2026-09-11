#include <assert.h>
#include <setjmp.h>
#include <sys/stat.h>
#include "os.h"

static int startup_stat(const char *, struct stat *);
static FILE *startup_fopen(const char *, const char *);
static int startup_fclose(FILE *);
static _Noreturn void startup_exit(int);

#define stat(path, status) startup_stat(path, status)
#define fopen startup_fopen
#define fclose startup_fclose
#define _exit startup_exit
#define main keysvc_main
#include "../keysvc.c"
#undef main
#undef _exit
#undef fclose
#undef fopen
#undef stat

static jmp_buf resident_exit;
static int resource_status, service_status, log_unavailable;
static unsigned stat_calls, open_calls, close_calls, service_calls, resident_calls;
static mode_t resource_mode;
static FILE *opened_log;

static int startup_stat(const char *path, struct stat *status)
{
	assert(strcmp(path, "/appdata/ndl/ndl_resources.tns") == 0);
	stat_calls++;
	memset(status, 0, sizeof *status);
	status->st_mode = resource_mode;
	return resource_status;
}

static FILE *startup_fopen(const char *path, const char *mode)
{
#ifdef LOG_PATH
	assert(strcmp(path, LOG_PATH) == 0);
	assert(stat_calls == 0);
#else
	assert(strcmp(path, resource_status == 0 ? "/appdata/ndl/keysvc.txt.tns"
					       : "/documents/ndl/keysvc.txt.tns") == 0);
	assert(stat_calls == 1);
#endif
	assert(strcmp(mode, "w") == 0 && open_calls++ == 0);
	if (!log_unavailable) {
		opened_log = tmpfile();
		assert(opened_log);
	}
	return opened_log;
}

static int startup_fclose(FILE *stream)
{
	assert(stream == opened_log && close_calls++ == 0);
	return fclose(stream);
}

static _Noreturn void startup_exit(int status)
{
	assert(status == 0 && service_calls == 1 && resident_calls == 1);
	longjmp(resident_exit, 1);
}

void send_key_event(struct s_ns_event *event, unsigned short code, BOOL up, BOOL repeat)
{
	(void)event;
	(void)code;
	(void)up;
	(void)repeat;
	assert(!"startup posted a key");
}

int16_t TI_NN_Read(nn_ch_t channel, unsigned timeout, void *bytes, unsigned capacity, uint32_t received)
{
	(void)channel;
	(void)timeout;
	(void)bytes;
	(void)capacity;
	(void)received;
	assert(!"startup read service traffic");
	return -1;
}

int16_t TI_NN_Write(nn_ch_t channel, const void *bytes, unsigned length)
{
	(void)channel;
	(void)bytes;
	(void)length;
	assert(!"startup wrote service traffic");
	return -1;
}

int16_t TI_NN_StartService(unsigned short service, void *context, void (*callback)(nn_ch_t, void *))
{
	assert(service == KEYSVC_SID && context == NULL && callback == serve);
	assert(service_calls++ == 0 && open_calls == 1);
	return service_status;
}

int nl_isstartup(void)
{
	return 1;
}

unsigned nl_hwsubtype(void)
{
	return 2;
}

void nl_set_resident(void)
{
	assert(service_status == 1 && service_calls == 1 && resident_calls++ == 0);
}

static void check_startup(int resource, mode_t mode, int registration, int unavailable)
{
	resource_status = resource;
	resource_mode = mode;
	service_status = registration;
	log_unavailable = unavailable;
	stat_calls = open_calls = close_calls = service_calls = resident_calls = 0;
	opened_log = NULL;
	if (!setjmp(resident_exit)) {
		assert(keysvc_main() == 0);
		assert(registration != 1 && resident_calls == 0);
	} else {
		assert(registration == 1 && resident_calls == 1);
		assert(close_calls == 0);
		if (opened_log) {
			char logged[160];
			rewind(opened_log);
			size_t length = fread(logged, 1, sizeof logged - 1, opened_log);
			logged[length] = '\0';
			assert(strstr(logged, "keysvc: startup=1 hwsubtype=2\n"));
			assert(strstr(logged, "TI_NN_StartService(0x4b45) = 1\n"));
			fclose(opened_log);
		}
	}
	assert(open_calls == 1 && service_calls == 1);
	assert(close_calls == (registration != 1 && !unavailable));
}

int main(void)
{
	const int registration[] = { 1, 0, -1 };
	for (unsigned i = 0; i < sizeof registration / sizeof registration[0]; i++)
		for (int unavailable = 0; unavailable < 2; unavailable++) {
			check_startup(0, S_IFREG, registration[i], unavailable);
			check_startup(0, S_IFDIR, registration[i], unavailable);
			check_startup(-1, 0, registration[i], unavailable);
		}
	puts("keysvc startup: root selection, logging failures and residency passed");
	return 0;
}
