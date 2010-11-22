#include "dir.h"
#include "file.h"
#include "macros.h"
#include "message.h"
#include "sandbox.h"
#include "services.h"
#include "sudo.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Return non-zero if the given name is a valid sandbox name.
 * (Positive logic.)
 */
int sandbox_valid(const char *name) {
	if (!name) { return 1; }
	if (NAME_MAX < strlen(name)) { return 0; }
	if (!strcmp("/", name)) { return 1; }
	regex_t regex;
	regcomp(&regex, "^[^./ \t\r\n][^/ \t\r\n]*$", REG_EXTENDED);
	int result = regexec(&regex, name, 0, 0, 0);
	regfree(&regex);
	return 0 == result;
}

/* Return non-zero if the given name exists.  Set the pointer if given (the
 * pointer must point to a buffer of at least PATH_MAX bytes).  This requires
 * sandbox_breakout to have run previously.
 * (Positive logic.)
 */
int sandbox_exists(const char *name, char *pathname) {
	if (!strcmp("/", name)) {
		if (pathname) { strcpy(pathname, name); }
		return 1;
	}
	char *dirname = 0;
	dirname = file_join("/var/sandboxes", name);
	if (pathname) { strcpy(pathname, dirname); }
	struct stat s;
	int result = !lstat(dirname, &s) && S_ISDIR(s.st_mode);
	free(dirname);
	return result;
}

/* Break the current process and all its future children out of the sandbox
 * by creating a chroot we're not in and ascending to the original root
 * directory.  If name is not a null pointer, set the name of the sandbox we
 * broke out of (the pointer must point to a buffer of at least NAME_MAX
 * bytes).
 */
int sandbox_breakout(char *name) {
	int result = -1;
	char *dirname = 0;

	/* Create a temporary directory, change to a directory outside it, and
	 * chroot into the temporary directory.
	 */
	char tmpdirname[] = "/tmp/sandbox_breakout-XXXXXX";
	WARN(!mkdtemp(tmpdirname), "mkdtemp");
	WARN(chdir("/"), "chdir");
	WARN(chroot(tmpdirname), "chroot");

	/* Get current working directory, which is the root of the sandbox
	 * we just left.  Save it if the caller provided a pointer.
	 */
	char buf[PATH_MAX];
	WARN(!getcwd(buf, PATH_MAX), "getcwd");
	if (name) {
		if (strcmp("/", buf)) { strcpy(name, basename(buf)); }
		else { strcpy(name, "/"); }
	}

	/* Ascend to the root root directory and chroot into it.
	 */
	dirname = file_join(buf, tmpdirname);
	while (strcmp("/", buf)) {
		WARN(chdir(".."), "chdir");
		WARN(!getcwd(buf, PATH_MAX), "getcwd");
	}
	WARN(chroot("."), "chroot");

	/* Clean up the temporary directory.
	 */
	WARN(rmdir(dirname), "rmdir");

	result = 0;
error:
	free(dirname);
	return result;
}

/* List all sandboxes.
 */
char **sandbox_list() {
	char **result = 0;
	int i = 0, ii = -1, j = 0;
	struct dirent **namelist = 0;

	if (sandbox_breakout(0)) { goto error; }
	if (0 > (ii = scandir("/var/sandboxes", &namelist, 0, alphasort))) {
		goto error;
	}
	FATAL(!(result = (char **)calloc(ii - 2 + 1, sizeof(char *))), "calloc");
	for (i = 0, j = 0; i < ii; ++i) {
		if ('.' == *namelist[i]->d_name) { continue; }
		if (!(result[j++] = strdup(namelist[i]->d_name))) {
			perror("strdup");
			util_ilist_free_partial((void **)result, 0, j, -1);
			free(result);
			result = 0;
			goto error;
		}
	}
	result[j] = 0;

error:
	util_ilist_free((void **)namelist, ii);
	free(namelist);
	return result;
}

/* Return the name of the current sandbox.
 */
char *sandbox_which() {
	char name[NAME_MAX];
	sandbox_breakout(name);
	char *result = 0;
	FATAL(!(result = strdup(name)), "strdup");
	return result;
}

static int _sandbox_clone(
	const char *srcname, const char *destname,
	const char * m, int m_args
) {
	int result = -1;
	struct stat s;
	int i, fd = -1;

	char buf[NAME_MAX];
	if (sandbox_breakout(buf)) { goto error; }
	if (!srcname) { srcname = buf; }
	char src[PATH_MAX], dest[PATH_MAX];
	if (!sandbox_exists(srcname, src)) {
		message("sandbox %s does not exist\n", srcname);
		errno = ENOENT;
		goto error;
	}
	if (sandbox_exists(destname, dest)) {
		message("sandbox %s exists\n", destname);
		errno = EEXIST;
		goto error;
	}
	switch (m_args) {
	case 1:
		message(m, destname);
		break;
	case 2:
		message(m, srcname, destname);
		break;
	}

	/* Make sure /var/sandboxes exists and is a directory.
	 */
	if (lstat("/var/sandboxes", &s)) {
		WARN(mkdir("/var/sandboxes", 0755), "mkdir");
	}
	else if (!S_ISDIR(s.st_mode)) { goto error; }

	/* Shallow copy most of the filesystem.
	 */
	{
		WARN(lstat(src, &s), "lstat");
		const char *exclude[] = {
			"/etc", "/var/sandboxes", "/root", "/home", 0
		};
		for (i = 0; exclude[i]; ++i) {
			exclude[i] = file_join(src, exclude[i]);
		}
		result = dir_shallowcopy(src, dest, s.st_dev, exclude);
		util_nlist_free((void **)exclude);
	}

	/* Shallow copy /etc from the appropriate source into the shadow
	 * directory and create a placeholder for FUSE to take over.
	 */
	{
		const char *exclude[] = {0};
		char fuse[PATH_MAX], shadowsrc[PATH_MAX], shadowdest[PATH_MAX];
		strcpy(fuse, dest);
		strcat(fuse, "/etc");
		WARN(mkdir(fuse, 0755), "mkdir");
		if (strcmp("/", srcname)) {
			strcpy(shadowsrc, "/var/sandboxes/.");
			strcat(shadowsrc, srcname);
			strcat(shadowsrc, "/etc");
		}
		else { strcpy(shadowsrc, "/etc"); }
		WARN(lstat(shadowsrc, &s), "lstat");
		strcpy(shadowdest, "/var/sandboxes/.");
		strcat(shadowdest, destname);
		WARN(mkdir(shadowdest, 0755), "mkdir");
		strcat(shadowdest, "/etc");
		result += dir_shallowcopy(shadowsrc, shadowdest, s.st_dev, exclude);
	}

	/* Deep copy /root and /home.
	 */
	{
		const char *exclude[] = {"/var/sandboxes", 0};
		for (i = 0; exclude[i]; ++i) {
			exclude[i] = file_join(src, exclude[i]);
		}
		const char *deepcopy[] = {"/root", "/home", 0};
		for (i = 0; deepcopy[i]; ++i) {
			char *deepsrc = file_join(src, deepcopy[i]);
			char *deepdest = file_join(dest, deepcopy[i]);
			result += dir_deepcopy(deepsrc, deepdest, exclude);
			free(deepsrc);
			free(deepdest);
		}
		util_nlist_free((void **)exclude);
	}

	/* Write the name of the parent sandbox to the `parent` file in the
	 * shadow directory.
	 */
	char parent[PATH_MAX];
	snprintf(parent, PATH_MAX, "/var/sandboxes/.%s/parent", destname);
	WARN(0 > (fd = open(parent, O_WRONLY | O_CREAT, 0644)), "open");
	if (strcmp("/", srcname)) {
		WARN(0 > write(fd, srcname, strlen(srcname)), "write");
		WARN(0 > write(fd, "\n", 1), "write");
	}

	result = 0;
error:
	close(fd);
	return result;
}

/* Create a sandbox by cloning the base sandbox.
 */
int sandbox_create(const char *name) {
	return _sandbox_clone("/", name, "creating sandbox %s\n", 1);
}

/* Clone a sandbox.
 */
int sandbox_clone(const char *srcname, const char *destname) {
	return _sandbox_clone(srcname, destname, "cloning sandbox %s to %s\n", 2);
}

/* Increment the named sandbox's reference count.  This must be called
 * *before* using the sandbox.  A file descriptor to the file remains
 * open and is returned for later calling `_sandbox_refcount_dec`.
 */
static int _sandbox_refcount_inc(const char *name) {
	int fd = -1;
	if (!strcmp("/", name)) { goto error; }
	char pathname[PATH_MAX];
	sprintf(pathname, "/var/sandboxes/.%s/refs", name);
	WARN(0 > (fd = open(pathname, O_RDWR | O_CREAT, 0644)), "open");
	struct flock lock;
	lock.l_type = F_RDLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 1;
	WARN(fcntl(fd, F_SETLKW, &lock), "fcntl");
	return fd;
error:
	return -1;
}

/* Return zero if this process is the last reference, in which case
 * it should clean up and then call `_sandbox_refcount_dec`.
 */
static int _sandbox_refcount_check(int fd) {
	if (0 > fd) { return -1; }
	struct flock lock;
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 1;
	if (fcntl(fd, F_SETLK, &lock)) {
		if (EAGAIN != errno) {
			perror("fcntl");
			return -1;
		}
		return 1;
	}
	return 0;
}

/* Decrement the reference count of the sandbox on the given file descriptor.
 */
static int _sandbox_refcount_dec(int fd) {
	if (0 > fd) { return -1; }
	int result = -1;
	struct flock lock;
	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 1;
	WARN(fcntl(fd, F_SETLK, &lock), "fcntl");
	result = 0;
error:
	close(fd);
	return result;
}

/* Run the user's preferred shell or an arbitrary command in a chroot.
 *   HERE BE DRAGONS: If command == 1, return rather than executing a shell
 *   or command.
 */
int sandbox_use(const char *name, const char *command, const char *callback) {
	int result = -1;
	int ref = -1;
	struct stat s1, s2;
	char *dirname2 = 0, *root = 0,
		*sockname = 0, *sockname2 = 0, *sockname3 = 0, *dirname3 = 0;
	const char *dirnames[] = {"/etc/init", "/etc/init.d", 0};
	struct dirent **namelists[] = {0, 0, 0};
	int jj[3];
	GHashTable *services = 0;
	pid_t pid;

	char buf[PATH_MAX];
	if (sandbox_breakout(buf)) { goto error; }
	char dirname1[PATH_MAX];
	if (!sandbox_exists(name, dirname1)) {
		message("sandbox %s does not exist\n", name);
		errno = ENOENT;
		goto error;
	}
	message("using sandbox %s\n", name);
	ref = _sandbox_refcount_inc(name);

	/* If the user's home directory doesn't exist in this sandbox, deep
	 * copy if from the base sandbox.
	 */
	char *homesrc, *homedest;
	WARN(!(homesrc = getenv("HOME")), "getenv");
	homedest = file_join(dirname1, homesrc);
	if (lstat(homedest, &s1)) {
		const char *exclude[] = {0};
		dir_deepcopy(homesrc, homedest, exclude);
	}
	free(homedest);

	/* Recursively rebind mounted devices in the sandbox using mount(8).
	 * This will only need to do actual work the first time a sandbox
	 * is used after a reboot.  Because walking the entire filesystem
	 * is slow, this guesses that if /dev is mounted correctly, so is
	 * everything else.
	 */
	WARN(lstat("/dev", &s1), "lstat");
	dirname2 = file_join(dirname1, "dev");
	WARN(lstat(dirname2, &s2), "lstat");
	if (s1.st_dev != s2.st_dev) {
		struct stat s;
		WARN(lstat(dirname1, &s), "lstat");
		message("remounting devices\n");
		const char *exclude[] = {"/var/sandboxes", "/root", "/home", 0};
		if (dir_remount("/", dirname1, s.st_dev, exclude)) { goto error; }
	}

	/* Mount FUSE in front of /etc if that hasn't already been done.
	 */
	WARN(lstat("/etc", &s1), "lstat");
	root = file_join(dirname1, "etc");
	WARN(lstat(root, &s2), "lstat");
	if (s1.st_dev == s2.st_dev && strcmp("/", name)) {
		message("mounting special /etc\n");
		WARN(0 > (pid = fork()), "fork");
		if (!pid) {
			execlp("sandboxfs", "sandboxfs", "-oallow_other", root, (char *)0);
			perror("execlp");
			exit(-1);
		}
		int status;
		do { wait(&status); }
		while (!WIFEXITED(status));
		if (WEXITSTATUS(status)) {
			message("sandboxfs misbehaving, skipping\n");
		}
	}

	/* If there's an `ssh-agent`(1) running in the current sandbox, copy it
	 * into the one being used.
	 */
	if ((sockname = getenv("SSH_AUTH_SOCK"))) {
		if (strcmp("/", buf)) {
			char tmp[NAME_MAX];
			strcpy(tmp, buf);
			strcpy(buf, "/var/sandboxes/");
			strcat(buf, tmp);
			strcat(buf, sockname);
		}
		else { strcpy(buf, sockname); }
		if (!lstat(buf, &s1)) {
			sockname2 = file_join(dirname1, sockname);
			FATAL(!(sockname3 = strdup(sockname2)), "strdup");
			dirname3 = dirname(sockname3);
			if (mkdir(dirname3, 0700) && EEXIST != errno) { perror("mkdir"); }
			if (chown(dirname3, s1.st_uid, s1.st_gid)) { perror("chown"); }
			if (link(buf, sockname2) && EEXIST != errno) { perror("link"); }
		}
	}

	/* Note services that exist in the base sandbox if we're starting an
	 * interactive shell.
	 */
	if (!command) {
		if (!(services = services_list(dirnames, namelists, jj))) {
			goto error;
		}
	}

	/* Use the sandbox.  Return if that's all we need.
	 */
	WARN(chroot(dirname1), "chroot");
	const char *home = getenv("HOME");
	WARN(!home, "getenv");
	WARN(chdir(home), "chdir");
	if ((void *)1 == command) {
		result = 0;
		goto error;
	}

	/* Execute the command (or the user's shell) followed by the callback
	 * as the user.
	 */
	const char *argv[] = {0, 0, 0, 0};
	if (command) {
		argv[0] = "/bin/sh";
		argv[1] = "-c";
		argv[2] = command;
	}
	else {
		argv[0] = getenv("SHELL") ?: "/bin/sh";
		argv[1] = "-i";
		argv[2] = "-l";
	}
	WARN(0 > (pid = fork()), "fork");
	if (!pid) {
		sudo_downgrade();
		execvp(argv[0], (char * const *)argv);
		perror("execvp");
		exit(-1);
	}
	int status;
	wait(&status);
	if (callback) {
		argv[0] = "/bin/sh";
		argv[1] = "-c";
		argv[2] = callback;
		WARN(0 > (pid = fork()), "fork");
		if (!pid) {
			sudo_downgrade();
			execv(argv[0], (char * const *)argv);
			perror("execv");
			exit(-1);
		}
		int status;
		wait(&status);
	}

	/* Offer to stop running services if this was an interactive
	 * shell.
	 */
	if (!command) {
		if (services_stop(dirnames, services)) { goto error; }
	}

	result = 0;
error:
	free(dirname2);
	free(root);
	free(sockname2);
	free(sockname3);

	/* If this process is the last one using this link to the
	 * `SSH_AUTH_SOCK`, remove it.
	 */
	if (!_sandbox_refcount_check(ref) && sockname) {
		unlink(sockname);
		FATAL(!(sockname2 = strdup(sockname)), "strdup");
		rmdir(dirname(sockname2));
		free(sockname2);
	}
	_sandbox_refcount_dec(ref);

	if (services) { g_hash_table_destroy(services); }
	util_nilist_free((void **)namelists, jj);
	return result;
}

/* Shortcut.
 */
static int _sandbox_use(const char *name) {
	message_quiet(1);
	int result = sandbox_use(name, (void *)1, 0);
	message_quiet(MESSAGE_QUIET_DEFAULT);
	return result;
}

/* Mark a file.  If the file is a shallow copy, make it a deep copy.  Set
 * the file's group to 48879 (the group the blueprinter will notice) and
 * give group write access to the file.
 */
int sandbox_mark(const char *name, const char *pathname) {
	int result = -1;
	char *pathname2 = 0, *tmpname = 0;

	/* Turn a potentially relative pathname into an absolute pathname
	 * before using the requested sandbox.
	 */
	if ('/' != *pathname) {
		char buf[PATH_MAX];
		WARN(!getcwd(buf, PATH_MAX), "getcwd");
		pathname2 = file_join(buf, pathname);
	}
	else { pathname2 = (char *)pathname; }

	char buf[NAME_MAX];
	if (sandbox_breakout(buf)) { goto error; }
	if (!name) { name = buf; }
	if (!sandbox_exists(name, 0)) {
		message("sandbox %s does not exist\n", name);
		errno = ENOENT;
		goto error;
	}
	if (_sandbox_use(name)) { goto error; }

	/* Stat the file and create it if it doesn't exist.
	 */
	struct stat s;
	if (lstat(pathname2, &s)) {
		int fd = creat(pathname2, 0644);
		WARN(0 > fd, "creat");
		WARN(close(fd), "close");
		WARN(stat(pathname2, &s), "stat");
	}

	/* Don't allow unlocking directories.
	 */
	if (S_ISDIR(s.st_mode)) {
		errno = EISDIR;
		goto error;
	}

	/* If the file is a symbolic link, change owning group and dereference
	 * it before making a deep copy.
	 */
	char buf2[PATH_MAX + 1], *pathname3 = 0;
	if (S_ISLNK(s.st_mode)) {
		WARN(lchown(pathname2, s.st_uid, 48879), "lchown");
		ssize_t len = readlink(pathname2, buf2, PATH_MAX);
		WARN(0 > len, "readlink");
		buf2[len] = 0; /* readlink(2) doesn't set a null terminator. */
		pathname3 = (char *)buf2;
		WARN(lstat(pathname3, &s), "lstat");
	}
	else { pathname3 = pathname2; }

	/* Deep copy this file.
	 */
	FATAL(!(tmpname = (char *)malloc(strlen(pathname3) + 8)), "malloc");
	strcpy(tmpname, pathname3);
	strcat(tmpname, "-XXXXXX");
	if (file_copy(pathname3, tmpname)) { goto error; }
	WARN(unlink(pathname3), "unlink");
	WARN(rename(tmpname, pathname3), "rename");

	/* Change the owning group to blueprint (48879) and allow members of
	 * the owning group to write the file.
	 */
	WARN(chown(pathname3, s.st_uid, 48879), "chown");
	WARN(chmod(pathname3, s.st_mode | S_IWGRP), "chmod");

	result = 0;
error:
	if ('/' != *pathname) { free(pathname2); }
	free(tmpname);
	return result;
}

/* Destroy a sandbox.
 */
int sandbox_destroy(const char *name) {

	char buf[NAME_MAX];
	if (sandbox_breakout(buf)) { goto error; }
	char dirname[PATH_MAX];
	if (!sandbox_exists(name, dirname)) {
		message("sandbox %s does not exist\n", name);
		errno = ENOENT;
		goto error;
	}
	if (!strcmp("/", name)) {
		message("won't destroy the base sandbox\n");
		goto error;
	}
	if (!strcmp(buf, name)) {
		message("won't destroy the current sandbox\n");
		goto error;
	}
	message("destroying sandbox %s\n", name);

	struct stat s1, s2;
	WARN(lstat(dirname, &s1), "lstat");

	char fuse[PATH_MAX], shadow[PATH_MAX];
	strcpy(fuse, dirname);
	strcat(fuse, "/etc");
	WARN(lstat(fuse, &s2), "lstat");
	if (s1.st_dev != s2.st_dev) {
		message("umnounting special /etc\n");
		pid_t pid;
		WARN(0 > (pid = fork()), "fork");
		if (!pid) {
			execl("/bin/umount", "umount", fuse, (char *)0);
			perror("execl");
			exit(-1);
		}
		int status;
		do { wait(&status); }
		while (!WIFEXITED(status));
		if (WEXITSTATUS(status)) {
			message("sandboxfs misbehaving, skipping\n");
		}
	}
	strcpy(shadow, "/var/sandboxes/.");
	strcat(shadow, name);
	if (!lstat(shadow, &s2)) {
		if (dir_unlink(shadow, s2.st_dev)) { goto error; }
	}

	if (dir_unlink(dirname, s1.st_dev)) { goto error; }

	return 0;
error:
	return -1;
}
