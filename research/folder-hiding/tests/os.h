#ifndef RELOCATION_TEST_OS_H
#define RELOCATION_TEST_OS_H

#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>

static int fixture_stat(const char *, struct stat *);
static FILE *fixture_fopen(const char *, const char *);
static int fixture_rename(const char *, const char *);
static int nl_isstartup(void);

#define stat(path, buffer) fixture_stat(path, buffer)
#define fopen(path, mode) fixture_fopen(path, mode)
#define rename(source, destination) fixture_rename(source, destination)

#ifdef ARM_BOOT_TEST
static size_t fixture_fread(void *, size_t, size_t, FILE *);
static size_t fixture_fwrite(const void *, size_t, size_t, FILE *);
static int fixture_ferror(FILE *);
static int fixture_fclose(FILE *);
#define fread(bytes, size, count, stream) fixture_fread(bytes, size, count, stream)
#define fwrite(bytes, size, count, stream) fixture_fwrite(bytes, size, count, stream)
#define ferror(stream) fixture_ferror(stream)
#define fclose(stream) fixture_fclose(stream)
#else
static int fixture_mkdir(const char *, mode_t);
static void refresh_osscr(void);
#define mkdir(path, mode) fixture_mkdir(path, mode)
#endif

#endif
