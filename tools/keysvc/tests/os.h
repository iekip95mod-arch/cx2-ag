#ifndef KEYSVC_TEST_OS_H
#define KEYSVC_TEST_OS_H

static volatile unsigned keysvc_test_restart_register;
#define KEYSVC_RESTART_REGISTER (&keysvc_test_restart_register)

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef int BOOL;
typedef void *nn_ch_t;

enum { FALSE = 0, TRUE = 1 };

struct s_ns_event {
	unsigned int timestamp;
	unsigned short type;
	unsigned short ascii;
	unsigned int key;
	unsigned int cursor_x;
	unsigned int cursor_y;
	unsigned int unknown;
	unsigned short modifiers;
	unsigned char click;
};

void send_key_event(struct s_ns_event *event, unsigned short code, BOOL up, BOOL repeat);
int16_t TI_NN_Read(nn_ch_t channel, unsigned timeout, void *bytes, unsigned capacity, uint32_t received);
int16_t TI_NN_Write(nn_ch_t channel, const void *bytes, unsigned length);
int16_t TI_NN_StartService(unsigned short service, void *context, void (*callback)(nn_ch_t, void *));
int nl_isstartup(void);
unsigned nl_hwsubtype(void);
void nl_set_resident(void);

#endif
