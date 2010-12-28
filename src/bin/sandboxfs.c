/*
 * Dependencies:
 *   apt-get install libfuse-dev
 * Testing:
 *   sudo bin/sandboxfs -oallow_other mnt -f
 *   ^Z
 *   sudo umount mnt && fg
 */

#define FUSE_USE_VERSION 26

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

static char *_shadow = 0;

/* Deep copy the file at the given pathname.  If fd is not negative and
 * flags are non-zero, fd will be made to refer to the the new deep copy.
 */
int _deepcopy(const char *pathname, int fd, int flags) {
	int shallow_fd = -1, deep_fd = -1;
	char *template = 0;

	/* There are some files that should never be deep copied. */
	if (!strcmp("/etc/resolv.conf", pathname)) { goto reopen; }

	/* Short circuit if this is already a deep copy. */
	struct stat s;
	if (lstat(pathname, &s)) {
		if (ENOENT != errno) { return -errno; }
		goto reopen;
	}
	if (1 == s.st_nlink) { goto reopen; }
	if (S_ISDIR(s.st_mode)) { goto reopen; }

	/* Open a temporary file to receive the deep copy. */
	if (!(template = (char *)malloc(strlen(pathname) + 8))) { return -errno; }
	strcpy(template, pathname);
	strcat(template, "-XXXXXX");
	errno = 0;
	if (0 > (deep_fd = mkstemp(template))) { goto error; }

	/* Buffered file copy. */
	if (0 > (shallow_fd = open(pathname, O_RDONLY))) { goto error; }
	char buf[4096];
	ssize_t len;
	while (0 < (len = read(shallow_fd, buf, 4096))) {
		if (0 > write(deep_fd, buf, len)) { goto error; }
	}
	if (0 > len) { goto error; }

	/* Move the deep copy into place. */
	if (rename(template, pathname)) { goto error; }

	/* Copy file metadata. */
	if (fchown(deep_fd, s.st_uid, s.st_gid)) { goto error; }
	if (fchmod(deep_fd, s.st_mode)) { goto error; }
	struct utimbuf times = {s.st_atime, s.st_mtime};
	if (utime(pathname, &times)) { goto error; }

	/* Reopen if necessary. */
reopen:
	if (0 <= fd && flags) {
		close(deep_fd);
		if (0 > (deep_fd = open(pathname, flags))) { goto error; }
		if (0 > dup2(deep_fd, fd)) { goto error; }
	}

error:
	close(shallow_fd);
	close(deep_fd);
	free(template);
	return -errno;
}

int _unroot() {
	struct fuse_context *context = fuse_get_context();
	if (0 > setfsgid(context->gid)) { return -EINVAL; }
	if (0 > setfsuid(context->uid)) { return -EINVAL; }
	return 0;
}

void *sandboxfs_init(struct fuse_conn_info *conn) {
	fprintf(stderr, "[sandboxfs] init conn: %p\n", conn);
	if (chroot(_shadow)) { perror("chroot"); } /* XXX Handle failure. */
	return 0;
}

void sandboxfs_destroy(void *ptr) {
	fprintf(stderr, "[sandboxfs] destroy ptr: %p\n", ptr);
}

int sandboxfs_access(const char *pathname, int mode) {
	fprintf(stderr, "[sandboxfs] access pathname: %s, mode: %o\n", pathname, mode);
	_unroot();
	if (access(pathname, mode)) { return -errno; }
	return 0;
}

#if 0
int sandboxfs_bmap(const char *pathname, size_t size, uint64_t *index) {
	fprintf(stderr, "[sandboxfs] bmap pathname: %s, size: %%ld, index: %p\n", pathname/*, size*/, index);
	_unroot();
	return 0;
}
#endif

int sandboxfs_chmod(const char *pathname, mode_t mode) {
	fprintf(stderr, "[sandboxfs] chmod pathname: %s, mode: %o\n", pathname, mode);
	_deepcopy(pathname, -1, 0);
	_unroot();
	if (chmod(pathname, mode)) { return -errno; }
	return 0;
}

int sandboxfs_chown(const char *pathname, uid_t uid, gid_t gid) {
	fprintf(stderr, "[sandboxfs] chown pathname: %s, uid: %d, gid: %d\n", pathname, uid, gid);
	_deepcopy(pathname, -1, 0);
	_unroot();
	if (lchown(pathname, uid, gid)) { return -errno; }
	return 0;
}

int sandboxfs_create(
	const char *pathname, mode_t mode, struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] create pathname: %s, mode: %o, fi: %p\n", pathname, mode, fi);
	_unroot();
	int fd;
	if (0 > (fd = creat(pathname, mode))) { return -errno; }
	fi->fh = (uint64_t)fd;
	return 0;
}

int sandboxfs_fgetattr(
	const char *pathname, struct stat *s, struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] fgetattr pathname: %s, s: %p, fi: %p\n", pathname, s, fi);
	int fd = (int)fi->fh;
	if (fstat(fd, s)) { return -errno; }
	return 0;
}

#if 0
int sandboxfs_flush(const char *pathname, struct fuse_file_info *fi) {
	fprintf(stderr, "[sandboxfs] flush pathname: %s, fi: %p\n", pathname, fi);
	return 0;
}
#endif

int sandboxfs_fsync(
	const char *pathname, int datasync, struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] fsync pathname: %s, datasync: %d, fi: %p\n", pathname, datasync, fi);
	int fd = (int)fi->fh;
	if (datasync) {
		if (fdatasync(fd)) { return -errno; }
	}
	else {
		if (fsync(fd)) { return -errno; }
	}
	return 0;
}

#if 0
int sandboxfs_fsyncdir(
	const char *pathname, int datasync, struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] fsyncdir pathname: %s, datasync: %d, fi: %p\n", pathname, datasync, fi);
	return 0;
}
#endif

int sandboxfs_ftruncate(
	const char *pathname, off_t off, struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] ftruncate pathname: %s, off: %%ld, fi: %p\n", pathname/*, off*/, fi);
	int fd = (int)fi->fh;
	_deepcopy(pathname, fd, fi->flags);
	if (ftruncate(fd, off)) { return -errno; }
	return 0;
}

int sandboxfs_getattr(const char *pathname, struct stat *s) {
	fprintf(stderr, "[sandboxfs] getattr pathname: %s, s: %p\n", pathname, s);
	_unroot();
	if (lstat(pathname, s)) { return -errno; }
	return 0;
}

#ifdef HAVE_GETXATTR
int sandboxfs_getxattr(
	const char *pathname, const char *name, char *buf, size_t size
) {
	fprintf(stderr, "[sandboxfs] getxattr pathname: %s, name: %s, buf: %p, size: %%ld\n", pathname, name, buf/*, size*/);
	_unroot();
	return 0;
}
#endif

#if 0
int sandboxfs_ioctl(
	const char *pathname,
	int cmd,
	void *arg,
	struct fuse_file_info *fi,
	unsigned int flags,
	void *ptr
) {
	fprintf(stderr, "[sandboxfs] ioctl pathname: %s, cmd: %d, arg: %p, fi: %p, flags: %o, ptr: %p\n", pathname, cmd, arg, fi, flags, ptr);
	_unroot();
	return 0;
}
#endif

int sandboxfs_link(const char *oldpathname, const char *newpathname) {
	fprintf(stderr, "[sandboxfs] link oldpathname: %s, newpathname: %s\n", oldpathname, newpathname);
	_unroot();
	if (link(oldpathname, newpathname)) { return -errno; }
	return 0;
}

#ifdef HAVE_LISTXATTR
int sandboxfs_listxattr(const char *pathname, char *name, size_t size) {
	fprintf(stderr, "[sandboxfs] listxattr pathname: %s, name: %p, size: %%ld\n", pathname, name/*, size*/);
	_unroot();
	return 0;
}
#endif

int sandboxfs_lock(
	const char *pathname,
	struct fuse_file_info *fi,
	int cmd,
	struct flock * arg
) {
	fprintf(stderr, "[sandboxfs] lock pathname: %s, fi: %p, cmd: %d, arg: %p\n", pathname, fi, cmd, arg);
	int fd = (int)fi->fh;
	if (fcntl(fd, cmd, arg)) { return -errno; }
	return 0;
}

int sandboxfs_mkdir(const char *pathname, mode_t mode) {
	fprintf(stderr, "[sandboxfs] mkdir pathname: %s, mode: %o\n", pathname, mode);
	_unroot();
	if (mkdir(pathname, mode)) { return -errno; }
	return 0;
}

int sandboxfs_mknod(const char *pathname, mode_t mode, dev_t dev) {
	fprintf(stderr, "[sandboxfs] mknod pathname: %s, mode: %o, dev: %%ld\n", pathname, mode/*, dev*/);
	_unroot();
	if (mknod(pathname, mode, dev)) { return -errno; }
	return 0;
}

int sandboxfs_open(const char *pathname, struct fuse_file_info *fi) {
	fprintf(stderr, "[sandboxfs] open pathname: %s, fi: %p\n", pathname, fi);
	_unroot();
	int fd;
	if (0 > (fd = open(pathname, fi->flags))) { return -errno; }
	fi->fh = (uint64_t)fd;
	return 0;
}

#if 0
int sandboxfs_opendir(const char *pathname, struct fuse_file_info *fi) {
	fprintf(stderr, "[sandboxfs] opendir pathname: %s, fi: %p\n", pathname, fi);
	_unroot();
	return 0;
}
#endif

#if 0
int sandboxfs_poll(
	const char *pathname,
	struct fuse_file_info *fi,
	struct fuse_pollhandle *ph,
	unsigned *reventsp
) {
	fprintf(stderr, "[sandboxfs] poll pathname: %s, fi: %p, ph: %p, reventsp: %p\n", pathname, fi, ph, reventsp);
	_unroot();
	return 0;
}
#endif

int sandboxfs_read(
	const char *pathname,
	char *buf,
	size_t size,
	off_t off,
	struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] read pathname: %s, buf: %p, size: %%ld, off: %%ld, fi: %p\n", pathname, buf/*, size, off*/, fi);
	int fd = (int)fi->fh;
	if ((off_t)-1 == lseek(fd, off, SEEK_SET)) { return -errno; }
	ssize_t result;
	if (0 > (result = read(fd, buf, size))) { return -errno; }
	return result;
}

int sandboxfs_readdir(
	const char *pathname,
	void *ptr,
	fuse_fill_dir_t filler,
	off_t off,
	struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] readdir pathname: %s, ptr: %p, filler: %p, off: %%ld, fi: %p\n", pathname, ptr, filler/*, off*/, fi);
	DIR *dir;
	if (!(dir = opendir(pathname))) { return -errno; }
	struct dirent *entry;
	errno = 0; /* For readdir(2) below. */
	while ((entry = readdir(dir))) { filler(ptr, entry->d_name, 0, 0); }
	if (closedir(dir)) { return -errno; }
	if (errno) { return -errno; } /* For readdir(2) above. */
	return 0;
}

int sandboxfs_readlink(const char *pathname, char *buf, size_t size) {
	fprintf(stderr, "[sandboxfs] readlink pathname: %s, buf: %p, size: %%ld\n", pathname, buf/*, size*/);
	_unroot();
	ssize_t len;
	if (0 > (len = readlink(pathname, buf, size))) { return -errno; }
	buf[len] = 0; /* readlink(2) doesn't set a null terminator. */
	return 0;
}

int sandboxfs_release(const char *pathname, struct fuse_file_info *fi) {
	fprintf(stderr, "[sandboxfs] release pathname: %s, fi: %p\n", pathname, fi);
	int fd = (int)fi->fh;
	if (close(fd)) { return -errno; }
	return 0;
}

#if 0
int sandboxfs_releasedir(const char *pathname, struct fuse_file_info *fi) {
	fprintf(stderr, "[sandboxfs] releasedir pathname: %s, fi: %p\n", pathname, fi);
	return 0;
}
#endif

#ifdef HAVE_REMOVEXATTR
int sandboxfs_removexattr(const char *pathname, const char *name) {
	fprintf(stderr, "[sandboxfs] removexattr pathname: %s, name: %s\n", pathname, name);
	_unroot();
	return 0;
}
#endif

int sandboxfs_rename(const char *oldpathname, const char *newpathname) {
	fprintf(stderr, "[sandboxfs] rename oldpathname: %s, newpathname: %s\n", oldpathname, newpathname);
	_deepcopy(oldpathname, -1, 0);
	_unroot();
	if (rename(oldpathname, newpathname)) { return -errno; }
	return 0;
}

int sandboxfs_rmdir(const char *pathname) {
	fprintf(stderr, "[sandboxfs] rmdir pathname: %s\n", pathname);
	_unroot();
	if (rmdir(pathname)) { return -errno; }
	return 0;
}

#ifdef HAVE_SETXATTR
int sandboxfs_setxattr(
	const char *pathname,
	const char *name,
	const char *value,
	size_t size,
	int flags
) {
	fprintf(stderr, "[sandboxfs] setxattr pathname: %s, name: %s, value: %p, size: %%ld, flags: %o\n", pathname, name, value/*, size*/, flags);
	_unroot();
	return 0;
}
#endif

int sandboxfs_statfs(const char *pathname, struct statvfs *s) {
	fprintf(stderr, "[sandboxfs] statfs pathname: %s, s: %p\n", pathname, s);
	_unroot();
	if (statvfs("/", s)) { return -errno; }
	return 0;
}

int sandboxfs_symlink(const char *target, const char *pathname) {
	fprintf(stderr, "[sandboxfs] symlink target: %s, pathname: %s\n", target, pathname);
	_unroot();
	if (symlink(target, pathname)) { return -errno; }
	return 0;
}

int sandboxfs_truncate(const char *pathname, off_t off) {
	fprintf(stderr, "[sandboxfs] truncate pathname: %s, off: %%ld\n", pathname/*, off*/);
	_deepcopy(pathname, -1, 0);
	_unroot();
	if (truncate(pathname, off)) { return -errno; }
	return 0;
}

int sandboxfs_unlink(const char *pathname) {
	fprintf(stderr, "[sandboxfs] unlink pathname: %s\n", pathname);
	_unroot();
	if (unlink(pathname)) { return -errno; }
	return 0;
}

int sandboxfs_utime(const char *pathname, struct utimbuf *times) {
	fprintf(stderr, "[sandboxfs] utime pathname: %s, times: %p\n", pathname, times);
	_deepcopy(pathname, -1, 0);
	_unroot();
	if (utime(pathname, times)) { return -errno; }
	return 0;
}

int sandboxfs_utimens(const char *pathname, const struct timespec tv[2]) {
	fprintf(stderr, "[sandboxfs] utimens pathname: %s, tv: %p\n", pathname, tv);
	_deepcopy(pathname, -1, 0);
	_unroot();
	if (utimensat(-1, pathname, tv, 0)) { return -errno; }
	return 0;
}

int sandboxfs_write(
	const char *pathname,
	const char *buf,
	size_t size,
	off_t off,
	struct fuse_file_info *fi
) {
	fprintf(stderr, "[sandboxfs] write pathname: %s, buf: %p, size: %%ld, off: %%ld, fi: %p\n", pathname, buf/*, size, off*/, fi);
	int fd = (int)fi->fh;
	_deepcopy(pathname, fd, fi->flags);
	if ((off_t)-1 == lseek(fd, off, SEEK_SET)) { return -errno; }
	ssize_t result;
	if (0 > (result = write(fd, buf, size))) { return -errno; }
	return result;
}

static struct fuse_operations sandboxfs_operations = {
	.init = sandboxfs_init,
	.destroy = sandboxfs_destroy,
	.access = sandboxfs_access,
	/* .bmap = sandboxfs_bmap, */
	.chmod = sandboxfs_chmod,
	.chown = sandboxfs_chown,
	.create = sandboxfs_create,
	.fgetattr = sandboxfs_fgetattr,
	/* .flush = sandboxfs_flush, */
	.fsync = sandboxfs_fsync,
	/* .fsyncdir = sandboxfs_fsyncdir, */
	.ftruncate = sandboxfs_ftruncate,
	.getattr = sandboxfs_getattr,
#ifdef HAVE_GETXATTR
	.getxattr = sandboxfs_getxattr,
#endif
	/* .ioctl = sandboxfs_ioctl, */
	.link = sandboxfs_link,
#ifdef HAVE_LISTXATTR
	.listxattr = sandboxfs_listxattr,
#endif
	.lock = sandboxfs_lock,
	.mkdir = sandboxfs_mkdir,
	.mknod = sandboxfs_mknod,
	.open = sandboxfs_open,
	/* .opendir = sandboxfs_opendir, */
	/* .poll = sandboxfs_poll, */
	.read = sandboxfs_read,
	.readdir = sandboxfs_readdir,
	.readlink = sandboxfs_readlink,
	.release = sandboxfs_release,
	/* .releasedir = sandboxfs_releasedir, */
#ifdef HAVE_REMOVEXATTR
	.removexattr = sandboxfs_removexattr,
#endif
	.rename = sandboxfs_rename,
	.rmdir = sandboxfs_rmdir,
#ifdef HAVE_SETXATTR
	.setxattr = sandboxfs_setxattr,
#endif
	.statfs = sandboxfs_statfs,
	.symlink = sandboxfs_symlink,
	.truncate = sandboxfs_truncate,
	.unlink = sandboxfs_unlink,
	.utime = sandboxfs_utime,
	.utimens = sandboxfs_utimens,
	.write = sandboxfs_write,
};

/* Decomposed fuse_main which allows us to get at the mountpoint.
 */
int main(int argc, char **argv) {
	char *mountpoint;
	int multithreaded, result;
	struct fuse *fuse = fuse_setup(
		argc, argv,
		&sandboxfs_operations, sizeof(sandboxfs_operations),
		&mountpoint,
		&multithreaded,
		0 /* void *user_data */
	);
	if (!fuse) { return 1; }

	/* Before we jump into the event loop, figure out the path
	 * to the shadow directory that contains our shallow copy.
	 */
	if (!(_shadow = (char *)malloc(strlen(mountpoint) + 2))) { return -1; }
	strcpy(&_shadow[1], mountpoint);
	strncpy(_shadow, "/var/sandboxes/.", strlen("/var/sandboxes/."));

	/* We're going to need pthread_cancel later and won't be able to find
	 * it post-chroot(2), so load it here.
	 */
	if (!dlopen("libgcc_s.so.1", RTLD_LAZY)) { return -1; }

	if (multithreaded) { result = fuse_loop_mt(fuse); }
	else { result = fuse_loop(fuse); }
	fuse_teardown(fuse, mountpoint);
	return 0 > result;
}
