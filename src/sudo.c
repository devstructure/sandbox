#include "macros.h"
#include "message.h"

#include <errno.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Execute the same program through sudo if we're not root.
 */
int sudo(int argc, char **argv) {
	if (!getuid()) { return 0; }
	char **argv2 = (char **)malloc((argc + 2) * sizeof(char **));
	FATAL(!argv2, "malloc");
	argv2[0] = "sudo";
	memcpy(&argv2[1], argv, argc * sizeof(char **));
	argv2[argc + 1] = 0;
	execvp(argv2[0], argv2);
	perror("execvp");
	return -1;
}

/* Downgrade privileges to the actual calling user (found through
 * SUDO_* environment variables) if this program was called through
 * sudo(1).  Call setgid(2) and initgroups(3) before setuid(2) forfeits
 * capabilities.
 */
int sudo_downgrade() {
	const char
		*sudo_user = getenv("SUDO_USER"),
		*sudo_uid = getenv("SUDO_UID"),
		*sudo_gid = getenv("SUDO_GID");
	if (!(sudo_user && sudo_uid && sudo_gid)) { goto error; }
	const char *user = sudo_user;
	uid_t uid = atoi(sudo_uid);
	gid_t gid = atoi(sudo_gid);
	if (!(user && uid && gid)) { goto error; }
	WARN(setgid(gid), "setgid");
	WARN(initgroups(user, gid), "initgroups");
	WARN(setuid(uid), "setuid");
	WARN(setenv("LOGNAME", user, 1), "setenv");
	WARN(setenv("USER", user, 1), "setenv");
	WARN(setenv("USERNAME", user, 1), "setenv");
	return 0;
error:
	return -1;
}
