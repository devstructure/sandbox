#include "file.h"
#include "macros.h"
#include "message.h"
#include "services.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <glib.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* List all the services in the given null-terminated list of directories,
 * using the given null-terminated list of namelists for storage and stores
 * the length of each namelist in the same index of jj.  Returns a pointer
 * to a new GHashTable for looking up these services.
 */
GHashTable *services_list(
	const char **dirnames, struct dirent ***namelists, int *jj
) {
	GHashTable *result = g_hash_table_new(g_str_hash, g_str_equal);
	FATAL(!result, "g_hash_table_new");
	int i, j;
	for (i = 0; dirnames[i]; ++i) {
		if (0 > (jj[i] = scandir(dirnames[i], &namelists[i], 0, alphasort))) {
			message_loud("scandir(%s, %p, 0, alphasort): %d\n",
				dirnames[i], &namelists[i], errno);
			util_nilist_free_partial((void **)namelists, jj, i, -1);
			g_hash_table_destroy(result);
			return 0;
		}
		for (j = 0; j < jj[i]; ++j) {
			if (!strcmp(".", namelists[i][j]->d_name)
				|| !strcmp("..", namelists[i][j]->d_name)
			) { continue; }
			g_hash_table_insert(result, namelists[i][j]->d_name, (void *)1);
		}
	}
	return result;
}

/* Find running services not listed in services and offer to stop them.
 */
int services_stop(const char **dirnames, GHashTable *services) {
	int result = -1;
	int i, j, jj = 0;
	struct dirent **namelist = 0;
	char *ptr = 0;
	pid_t pid;
	for (i = 0; dirnames[i]; ++i) {
		WARN(0 > (jj = scandir(dirnames[i], &namelist, 0, alphasort)),
			"scandir");
		for (j = 0; j < jj; ++j) {
			if (!strcmp(".", namelist[j]->d_name)
				|| !strcmp("..", namelist[j]->d_name)
			) { continue; }
			if (g_hash_table_lookup(services, namelist[j]->d_name)) {
				continue;
			}
			char *argv[] = {0, 0, 0};

			/* Check the status of services that only exist in the sandbox.
			 */
			char *service = namelist[j]->d_name;
			regex_t regex;
			regcomp(&regex, "\\.conf$", REG_EXTENDED);
			int upstart = !regexec(&regex, namelist[j]->d_name, 0, 0, 0);
			regfree(&regex);
			if (upstart) {
				argv[0] = "/sbin/status";
				argv[1] = ptr = service = strdup(namelist[j]->d_name);
				FATAL(!ptr, "strdup");
				argv[1][strlen(argv[1]) - 5] = 0;
			}
			else {
				argv[0] = ptr = file_join(dirnames[i], namelist[j]->d_name);
				argv[1] = "status";
			}
			WARN(0 > (pid = fork()), "fork");
			if (!pid) {
				stdin = freopen("/dev/null", "r", stdin);
				stdout = freopen("/dev/null", "w", stdout);
				stderr = freopen("/dev/null", "w", stderr);
				execv(argv[0], (char * const *)argv);
				perror("execv");
				exit(-1);
			}
			int status;
			wait(&status);
			if (!WIFEXITED(status) || WEXITSTATUS(status)) {
				free(ptr);
				ptr = 0;
				continue;
			}

			/* Offer to stop services that are running.
			 */
			errno = 0;
			while (!errno) {
				message_loud("stop service %s? [Yn] ", service);
				char buf[LINE_MAX];
				if (!fgets(buf, LINE_MAX, stdin)) {
					fputc('\n', stderr);
					continue;
				}
				if (!strcmp("\n", buf) || !strcasecmp("y\n", buf)) {}
				else if (!strcasecmp("n\n", buf)) { break; }
				else { continue; }
				if (upstart) { argv[0] = "/sbin/stop"; }
				else { argv[1] = "stop"; }
				WARN(0 > (pid = fork()), "fork");
				if (!pid) {
					stdin = freopen("/dev/null", "r", stdin);
					execv(argv[0], (char * const *)argv);
					perror("execv");
					exit(-1);
				}
				int status;
				wait(&status);
				break;
			}

			free(ptr);
			ptr = 0; /* Prevent double-free. */
		}
		util_ilist_free((void *)namelist, jj);
		free(namelist);
		namelist = 0; /* Prevent double-free. */
	}
	result = 0;
error:
	util_ilist_free((void *)namelist, jj);
	free(namelist);
	free(ptr);
	return result;
}
