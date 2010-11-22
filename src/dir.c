#include "dir.h"
#include "file.h"
#include "macros.h"
#include "message.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

#ifndef MNT_DETACH
#define MNT_DETACH 2
#endif

/* Walk a directory tree recursively, executing callbacks along the way.
 * Subdirectories will be processed in parallel if forks is greater than
 * zero.  Forks is decremented each time we descend into a new level.
 */
int dir_walk(
	const char *src, const char *dest,
	const char **exclude,
	dev_t dev,
	int(*dev_cb)(
		const char *src, const char *dest,
		dev_t dev,
		const struct stat *s,
		void *ptr
	),
	int(*before_cb)(
		const char *src, const char *dest,
		const struct stat *s,
		void *ptr
	),
	int(*symlink_cb)(
		const char *src, const char *dest,
		const char *basename, const char *pathname,
		const struct stat *s,
		void *ptr
	),
	int(*hardlink_cb)(
		const char *src, const char *dest,
		const char *basename, const char *pathname,
		const struct stat *s,
		void *ptr
	),
	int(*after_cb)(
		const char *src, const char *dest,
		const struct stat *s,
		void *ptr
	),
	void *ptr,
	const char *m, /* "walking %s" */
	int forks
) {
	int result = -1;
	int i;
	DIR *dirp = 0;
	char *pathname = 0;
	int children = 0;

	if (m) { message(m, src); }

	/* Don't recurse into yourself or excluded directories.
	 */
	char *srcsrc = strdup(src);
	FATAL(!srcsrc, "strdup");
	srcsrc = (char *)realloc(srcsrc, 2 + 2 * strlen(src));
	FATAL(!srcsrc, "realloc");
	strcat(srcsrc, src);
	if (!strcmp(srcsrc, dest)) {
		free(srcsrc);
		return 0;
	}
	free(srcsrc);
	for (i = 0; exclude[i]; ++i) {
		if (!strcmp(src, exclude[i])) { return 0; }
	}

	struct stat s;
	WARN(lstat(src, &s), "lstat");

	/* Handle device boundaries.  Differentiate between false and failure.
	 */
	if (dev_cb) {
		switch (dev_cb(src, dest, dev, &s, ptr)) {
		case 0:
			break;
		case 1:
			return 0;
		default:
			return -1;
		}
	}

	/* Recreate the source in the destination directory.
	 */
	if (before_cb && before_cb(src, dest, &s, ptr)) { goto error; }
	WARN(!(dirp = opendir(src)), "opendir");
	struct dirent *entry;
	while ((entry = readdir(dirp))) {
		char *basename = entry->d_name;
		if (!strcmp(".", basename)) { continue; }
		if (!strcmp("..", basename)) { continue; }
		pathname = file_join(src, basename);
		struct stat s2;
		WARN(lstat(pathname, &s2), "lstat");

		/* Handle symbolic links.
		 */
		if (S_ISLNK(s2.st_mode)) {
			if (symlink_cb && symlink_cb(
				src, dest,
				basename, pathname,
				&s2,
				ptr
			)) { goto error; }
		}

		/* Recurse into directories.
		 */
		else if (S_ISDIR(s2.st_mode)) {
			if (forks) {
				pid_t pid = fork();
				WARN(-1 == pid, "fork");
				if (pid) { ++children; }
				else {
					char *dest2 = file_join(dest, basename);
					dir_walk(
						pathname, dest2,
						exclude,
						dev,
						dev_cb, before_cb, symlink_cb, hardlink_cb, after_cb,
						ptr,
						m,
						forks - 1
					);
					free(dest2);
					exit(0);
				}
			}
			else {
				char *dest2 = file_join(dest, basename);
				dir_walk(
					pathname, dest2,
					exclude,
					dev,
					dev_cb, before_cb, symlink_cb, hardlink_cb, after_cb,
					ptr,
					m,
					0
				);
				free(dest2);
			}
		}

		/* Handle hard links.
		 */
		else if (hardlink_cb && hardlink_cb(
			src, dest, basename, pathname, &s2, ptr
		)) {
			goto error;
		}

		free(pathname);
	}
	pathname = 0; /* Prevent double-free. */

	result = 0;
error:
	closedir(dirp);
	free(pathname);

	/* Wait on children.
	 */
	for (; children > 0; --children) {
		int status;
		wait(&status);
	}

	/* Clean up directory.
	 */
	if (after_cb && after_cb(src, dest, &s, ptr)) { result = -1; }

	return result;
}

/* Copy a directory tree and execute callbacks on the non-directory links
 * within each one.
 */
int dir_copy_before(
	const char *src, const char *dest, const struct stat *s, void *ptr
) {
	WARN(mkdir(dest, s->st_mode), "mkdir");
	WARN(lchown(dest, s->st_uid, s->st_gid), "lchown");
	WARN(chmod(dest, s->st_mode), "chmod");
	return 0;
error:
	return -1;
}
int dir_copy_after(
	const char *src, const char *dest, const struct stat *s, void *ptr
) {
	struct utimbuf times = {s->st_atime, s->st_mtime};
	if (utime(dest, &times) && ENOENT != errno) { WARN(1, "utime"); }
	return 0;
error:
	return -1;
}
int dir_copy(
	const char *src, const char *dest,
	const char **exclude,
	dev_t dev,
	int(*dev_cb)(
		const char *src, const char *dest,
		dev_t dev,
		const struct stat *s,
		void *ptr
	),
	int(*symlink_cb)(
		const char *src, const char *dest,
		const char *basename, const char *pathname,
		const struct stat *s,
		void *ptr
	),
	int(*hardlink_cb)(
		const char *src, const char *dest,
		const char *basename, const char *pathname,
		const struct stat *s,
		void *ptr
	),
	void *ptr,
	const char *m, /* "walking %s" */
	int forks
) {
	return dir_walk(
		src, dest,
		exclude,
		dev,
		dev_cb,
		dir_copy_before,
		symlink_cb,
		hardlink_cb,
		dir_copy_after,
		ptr,
		m,
		forks
	);
}

/* Remount a device in a new location.  This is equivalent to mount(8)'s
 * --rbind option.  This purposely does not recurse into /proc rebinds
 * because that tends to cause errors.
 * TODO Recurse through more than one level of child devices.
 */
static int _dir_mount_dev(
	const char *src, const char *dest,
	dev_t dev,
	const struct stat *s,
	void *ptr
) {
	if (dev == s->st_dev) { return 0; } /* Keep going. */
	message("recursively mounting %s\n", src);
	WARN(mount(src, dest, 0, MS_BIND, 0), "mount");
	return 1; /* Don't descend. */
error:
	return -1;
}
int dir_mount(const char *src, const char *dest, dev_t dev) {
	message("mounting %s\n", src);
	WARN(mount(src, dest, 0, MS_BIND, 0), "mount");
	if (!strcmp("/proc", &src[strlen(src) - 5])) { return 0; }
	const char *exclude[] = {0};
	return dir_walk(
		src, dest,
		exclude,
		dev,
		_dir_mount_dev,
		0,
		0,
		0,
		0,
		0,
		0,
		0
	);
error:
	return -1;
}

/* Unmount a device lazily.  This is equivalent to umount(8)'s -l option.
 * This purposely does not recurse into /proc remounts because that tends
 * to cause errors.
 * TODO Recurse through more than one level of child devices.
 */
static int _dir_umount_dev(
	const char *src, const char *dest,
	dev_t dev,
	const struct stat *s,
	void *ptr
) {
	if (dev == s->st_dev) { return 0; } /* Keep going. */
	message("recursively unmounting %s\n", src);
	WARN(umount2(src, MNT_DETACH), "umount2");
	return 1; /* Don't descend. */
error:
	return -1;
}
int dir_umount(const char *dirname, dev_t dev) {
	message("unmounting %s\n", dirname);
	if (!strcmp("/proc", &dirname[strlen(dirname) - 5])) {
		const char *exclude[] = {0};
		if (dir_walk(
			dirname, dirname,
			exclude,
			dev,
			_dir_umount_dev,
			0,
			0,
			0,
			0,
			0,
			0,
			0
		)) { goto error; }
	}
	WARN(umount2(dirname, MNT_DETACH), "umount2");
	return 0;
error:
	return -1;
}

/* If we're at a device boundary, create a placeholder and move on.
 */
int dir_shallowcopy_dev(
	const char *src, const char *dest,
	dev_t dev,
	const struct stat *s,
	void *ptr
) {
	if (dev == s->st_dev) { return 0; } /* Keep going. */
	dir_copy_before(src, dest, s, ptr);
	dir_copy_after(src, dest, s, ptr);
	dir_mount(src, dest, s->st_dev);
	return 1; /* Don't descend. */
}

/* Hard link symbolic links so they remain symbolic links.
 */
int dir_shallowcopy_symlink(
	const char *src, const char *dest,
	const char *basename, const char *pathname,
	const struct stat *s,
	void *ptr
) {
	int result = -1;
	char *pathname2 = 0;
	pathname2 = file_join(dest, basename);
	WARN(link(pathname, pathname2), "link");
	result = 0;
error:
	free(pathname2);
	return result;
}

/* Hard link normal files unless they're setuid, setgid, sticky, or not a
 * regular file, symbolic link, directory, FIFO, or socket.  These special
 * conditions are a in response to dpkg(1)'s procedure for removing previous
 * versions of files.  A new inode is allocated for the new version and it
 * is swapped into place.  As a security measure, old versions that meet the
 * above criteria have their mode set to 0600 right before they're unlinked.
 * See chmodsafe_unlink_statted in src/help.c in the dpkg(1) source code for
 * more detail.  We add block and character special files to the list of
 * conditions because we still want to hard link these.
 * https://github.com/devstructure/contractor/issues/#issue/2
 */
int dir_shallowcopy_hardlink(
	const char *src, const char *dest,
	const char *basename, const char *pathname,
	const struct stat *s,
	void *ptr
) {
	int result = -1;
	char *pathname2 = 0;
	pathname2 = file_join(dest, basename);
	if (s->st_mode & (S_ISUID | S_ISGID | S_ISVTX) || !(s->st_mode & (
		S_IFREG | S_IFLNK | S_IFDIR | S_IFIFO | S_IFSOCK | S_IFBLK | S_IFCHR
	))) {
		if (file_copy(pathname, pathname2)) { goto error; }
	}

	/* Don't shallow copy `ssh-agent`(1) sockets. */
	else if (S_ISSOCK(s->st_mode)
		&& !strncmp("/tmp/ssh-", pathname, 9)
		&& !strncmp("agent.", basename, 6)
	) { rmdir(dest); }

	else { WARN(link(pathname, pathname2), "link"); }
	result = 0;
error:
	free(pathname2);
	return result;
}

/* Shallow copy a directory tree, attempting to rebind other devices.
 * Excluded directories will not be copied.
 */
int dir_shallowcopy(
	const char *src, const char *dest, dev_t dev, const char **exclude
) {
	return dir_copy(
		src, dest,
		exclude,
		dev,
		dir_shallowcopy_dev,
		dir_shallowcopy_symlink,
		dir_shallowcopy_hardlink,
		0,
		"shallow copying %s\n",
		3
	);
}

/* Create new symbolic links that will look just like the old symbolic
 * links.
 */
static int _dir_deepcopy_symlink(
	const char *src, const char *dest,
	const char *basename, const char *pathname,
	const struct stat *s,
	void *ptr
) {
	int result = -1;
	char *pathname2 = 0;
	char buf[PATH_MAX + 1];
	ssize_t len = readlink(pathname, buf, PATH_MAX);
	WARN(0 > len, "readlink");
	buf[len] = 0; /* readlink(2) doesn't set a null terminator. */
	pathname2 = file_join(dest, basename);
	WARN(symlink(buf, pathname2), "symlink");
	WARN(lchown(pathname2, s->st_uid, s->st_gid), "lchown");
	/*
	struct utimbuf times = {s->st_atime, s->st_mtime};
	WARN(utime(pathname2, &times), "utime");
	*/
	result = 0;
error:
	free(pathname2);
	return result;
}

/* Copy regular files.
 */
static int _dir_deepcopy_hardlink(
	const char *src, const char *dest,
	const char *basename, const char *pathname,
	const struct stat *s,
	void *ptr
) {
	int result = -1;
	char *pathname2 = 0;
	pathname2 = file_join(dest, basename);
	if (file_copy(pathname, pathname2)) { goto error; }
	result = 0;
error:
	free(pathname2);
	return result;
}

/* Deep copy a directory tree.  Excluded directories will not be copied.
 */
int dir_deepcopy(const char *src, const char *dest, const char **exclude) {
	return dir_copy(
		src, dest,
		exclude,
		0,
		0,
		_dir_deepcopy_symlink,
		_dir_deepcopy_hardlink,
		0,
		"deep copying %s\n",
		3
	);
}

/* Recursively remount all devices in a directory tree.
 */
static int _dir_remount_dev(
	const char *src, const char *dest,
	dev_t dev,
	const struct stat *s,
	void *ptr
) {
	if (dev == s->st_dev) { return 0; } /* Keep going. */
	message("mounting %s\n", dest);
	struct stat s2;
	WARN(lstat(dest, &s2), "lstat");
	if (s->st_dev != s2.st_dev) { dir_mount(src, dest, s->st_dev); }
	return 1; /* Don't descend. */
error:
	return -1;
}
int dir_remount(
	const char *src, const char *dest, dev_t dev, const char **exclude
) {
	return dir_walk(
		src, dest,
		exclude,
		dev,
		_dir_remount_dev,
		0,
		0,
		0,
		0,
		0,
		0,
		3
	);
}

/* If we're at a device boundary, unmount and move on.
 */
static int _dir_unlink_dev(
	const char *src, const char *dest,
	dev_t dev,
	const struct stat *s,
	void *ptr
) {
	if (dev == s->st_dev) { return 0; } /* Keep going. */
	dir_umount(src, s->st_dev);
	WARN(rmdir(src), "rmdir");
	return 1; /* Don't descend. */
error:
	return -1;
}

/* Unlink symbolic and hard links.
 */
static int _dir_unlink_link(
	const char *src, const char *dest,
	const char *basename, const char *pathname,
	const struct stat *s,
	void *ptr
) {
	WARN(unlink(pathname), "unlink");
	return 0;
error:
	return -1;
}

/* Clean up directories.
 */
static int _dir_unlink_after(
	const char *src, const char *dest,
	const struct stat *s,
	void *ptr
) {
	WARN(rmdir(src), "rmdir");
	return 0;
error:
	return -1;
}

/* Perform the equivalent of `rm -rf` on the given directory tree, attempting
 * to unmount other devices.
 */
int dir_unlink(const char *dirname, dev_t dev) {
	const char *exclude[] = {0};
	return dir_walk(
		dirname, dirname,
		exclude,
		dev,
		_dir_unlink_dev,
		0,
		_dir_unlink_link,
		_dir_unlink_link,
		_dir_unlink_after,
		0,
		"unlinking %s\n",
		0
	);
}
