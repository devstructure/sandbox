#include "macros.h"
#include "message.h"

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static __thread char *_message_prefix = 0;
static __thread size_t _message_prefix_len = 0;
static __thread int _message_quiet_default = 0;
static __thread int _message_quiet = 0;

int message_init(const char *progname) {
	if (_message_prefix) { return 0; }
	int result = -1;
	char *progname2 = strdup(progname);
	FATAL(!progname2, "strdup");
	char *b = basename(progname2);
	FATAL(!(_message_prefix = (char *)malloc(6 + strlen(b))), "malloc");
	_message_prefix_len = sprintf(_message_prefix, "# [%s] ", b);
	result = 0;
	free(progname2);
	return result;
}

void message_free() { free(_message_prefix); }

void message_quiet_default(int quiet) { _message_quiet_default = quiet; }

void message_quiet(int quiet) {
	if (MESSAGE_QUIET_DEFAULT == quiet) {
		_message_quiet = _message_quiet_default;
	}
	else { _message_quiet = quiet; }
}

static int _message(const char *format, va_list *ap) {
	int result = -1;
	if (!_message_prefix) { errno = EFAULT; goto error; }
	char buf[LINE_MAX];
	strcpy(buf, _message_prefix);
	vsnprintf(
		buf + _message_prefix_len,
		LINE_MAX - _message_prefix_len,
		format,
		*ap
	);
	fwrite(buf, 1, strlen(buf), stderr);
error:
	va_end(*ap);
	return result;
}

int message(const char *format, ...) {
	if (_message_quiet) { return 0; }
	va_list ap;
	va_start(ap, format);
	return _message(format, &ap);
}

int message_loud(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	return _message(format, &ap);
}
