#ifndef NSPIRE_TRACE_H
#define NSPIRE_TRACE_H

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int nspire_trace_enabled(void) {
	const char *enabled = getenv("NSPIRE_TRACE");
	return enabled && !strcmp(enabled, "1");
}

static inline void nspire_trace(const char *format, ...) {
	int saved_errno = errno;
	if (nspire_trace_enabled()) {
		va_list args;
		va_start(args, format);
		fputs("nspire-trace ", stderr);
		vfprintf(stderr, format, args);
		fputc('\n', stderr);
		va_end(args);
	}
	errno = saved_errno;
}

#endif
