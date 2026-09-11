#ifndef KEYSVC_RESTART_H
#define KEYSVC_RESTART_H

#ifndef KEYSVC_RESTART_REGISTER
#define KEYSVC_RESTART_REGISTER ((volatile unsigned *)0x90140020)
#endif

static void restart_handheld(void)
{
	*KEYSVC_RESTART_REGISTER = 0x80;
}

#endif
