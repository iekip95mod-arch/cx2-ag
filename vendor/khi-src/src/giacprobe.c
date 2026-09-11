#include <os.h>
#include "luabridge.h"

/* Splits the restart in half. luagiac fails somewhere between load and a first evaluation, and the
   Lua bridge, the OS Lua state and Giac itself are all candidates. This links the same Giac objects
   with no Lua involved at all and runs from the startup folder, so what survives to the file says
   where the fault is:

     no file at all      fault before main, so Giac's static constructors
     "start" only        fault inside the first giac_caseval, so Giac init or eval
     "1+1 = 2" then stop fault in the harder evaluation, so Giac works and something deeper breaks
     all four lines      Giac core is fine and the fault is in the Lua bridge or its memory context

   Each line is flushed because a program that faults never reaches fclose. */

#define OUT "/documents/ndl/giac-probe.txt.tns"

int main(void)
{
	/* Runs from the startup folder, so a fault here would restart the calculator into running it
	   again. The output file is written before anything that can fault, so its presence means this
	   already ran and a boot loop becomes one crash. Delete it to re-arm the probe. */
	FILE *prev = fopen(OUT, "r");
	if (prev) {
		fclose(prev);
		return 0;
	}

	FILE *f = fopen(OUT, "w");
	if (!f)
		return 1;

	fputs("start\n", f);
	fflush(f);

	const char *sum = giac_caseval("1+1");
	fprintf(f, "1+1 = %s\n", sum ? sum : "(null)");
	fflush(f);

	const char *d = giac_caseval("diff(x^2*sin(x),x)");
	fprintf(f, "diff = %s\n", d ? d : "(null)");
	fflush(f);

	fputs("done\n", f);
	fclose(f);
	return 0;
}
