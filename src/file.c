#include "file.h"
#include "macros.h"
#include "message.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

char *file_join(const char *dirname, const char *basename) {
	char *result = 0;
	size_t len = strlen(dirname) + strlen(basename) + 1;
	const char *slash = "";
	size_t offset = 0;
	if ('/' != dirname[strlen(dirname) - 1]) {
		++len;
		slash = "/";
	}
	if ('/' == basename[0]) {
		--len;
		offset = 1;
	}
	FATAL(!(result = (char *)malloc(len)), "malloc");
	sprintf(result, "%s%s%s", dirname, slash, &basename[offset]);
	return result;
}

int file_copy(const char *pathname1, const char *pathname2) {
	int result = -1;
	int fd1 = -1, fd2 = -1;
	struct stat s;
	WARN(lstat(pathname1, &s), "lstat");
	WARN(0 > (fd1 = open(pathname1, O_RDONLY)), "open");
	WARN(0 > (fd2 = open(pathname2, O_WRONLY|O_CREAT, s.st_mode)), "open");
	char buf[4096];
	int len = -1;
	while (0 < (len = read(fd1, buf, 4096))) {
		WARN(0 > write(fd2, buf, len), "write");
	}
	WARN(0 > len, "read");
	WARN(lchown(pathname2, s.st_uid, s.st_gid), "lchown");
	WARN(chmod(pathname2, s.st_mode), "chmod");
	struct utimbuf times = {s.st_atime, s.st_mtime};
	WARN(utime(pathname2, &times), "utime");
	result = 0;
error:
	close(fd1);
	close(fd2);
	return result;
}
