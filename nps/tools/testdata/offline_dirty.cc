// The fixture the offline audit has to reject: an ndl program that reaches a host both ways,
// over the link protocol and over the USB device stack. It is never deployed, only linked.
#include <libndls.h>
#include <syscall-decls.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char byte = 0;
    TI_NN_Write(0, &byte, 1);
    usbd_sync_transfer(0);
    return 0;
}
