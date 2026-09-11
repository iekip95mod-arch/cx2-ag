#include <os.h>
#include "restart.h"

int main(void)
{
	if (nl_isstartup() || nl_hwsubtype() != 2)
		return 1;
	restart_handheld();
	return 0;
}
