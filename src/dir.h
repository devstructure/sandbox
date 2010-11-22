#ifndef DIR_H
#define DIR_H

#include <sys/stat.h>
#include <sys/types.h>

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
);

int dir_copy_before(
	const char *src, const char *dest, const struct stat *s, void *ptr
);
int dir_copy_after(
	const char *src, const char *dest, const struct stat *s, void *ptr
);
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
);

int dir_mount(const char *src, const char *dest, dev_t dev);
int dir_umount(const char *dirname, dev_t dev);

int dir_shallowcopy_dev(
	const char *src, const char *dest,
	dev_t dev,
	const struct stat *s,
	void *ptr
);
int dir_shallowcopy_symlink(
	const char *src, const char *dest,
	const char *basename, const char *pathname,
	const struct stat *s,
	void *ptr
);
int dir_shallowcopy_hardlink(
	const char *src, const char *dest,
	const char *basename, const char *pathname,
	const struct stat *s,
	void *ptr
);
int dir_shallowcopy(
	const char *src, const char *dest, dev_t dev, const char **exclude
);

int dir_deepcopy(const char *src, const char *dest, const char **exclude);

int dir_remount(
	const char *src, const char *dest, dev_t dev, const char **exclude
);

int dir_unlink(const char *dirname, dev_t dev);

#endif
