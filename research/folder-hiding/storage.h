#ifndef CX2_STORAGE_H
#define CX2_STORAGE_H

#include <stddef.h>
#include <stdio.h>

int storage_inventory(const char *docroot, FILE *log);
int storage_hide(const char *docroot, const char *storeroot, char *error, size_t error_size);
int storage_restore(const char *docroot, const char *storeroot, char *error, size_t error_size);

#endif
