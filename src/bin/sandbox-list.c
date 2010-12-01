#include "../message.h"
#include "../sandbox.h"
#include "../sudo.h"

#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void usage(char *argv0) {
	fprintf(stderr,
		"Usage: %s [-n] [-q]\n",
		basename(argv0)
	);
}

void help() {
	fprintf(stderr,
		"  -n, --names show names only; do not indicate the current sandbox\n"
		"  -q, --quiet operate quietly\n"
		"  -h, --help  show this help message\n"
	);
}

int main(int argc, char **argv) {
	sudo(argc, argv);
	message_init(*argv);

	int names_only = 0;
	const char *optstring = "nqh";
	static struct option longopts[] = {
		{"names", 0, 0, 0},
		{"quiet", 0, 0, 0},
		{"help", 0, 0, 0},
		{0, 0, 0, 0}
	};
	int c = -1, longindex = 0;
	while (-1 != (c = getopt_long(
		argc, argv, optstring, longopts, &longindex
	))) {
		switch (c) {
		case 0:
			switch (longindex) {
			case 0: /* --names */
				names_only = 1;
				break;
			case 1: /* --quiet */
				message_quiet_default(1);
				message_quiet(1);
				break;
			case 2: /* --help */
				usage(*argv);
				help();
				exit(0);
			}
			break;
		case 'n': /* -n */
			names_only = 1;
			break;
		case 'q': /* -q */
			message_quiet_default(1);
			message_quiet(1);
			break;
		case 'h': /* -h */
			usage(*argv);
			help();
			exit(0);
			break;
		case '?':
			usage(*argv);
			exit(1);
			break;
		}
	}
	switch (argc - optind) {
	case 0:
		break;
	default:
		usage(*argv);
		exit(1);
		break;
	}

	/* Get the name of the current sandbox before we start breaking out.
	 */
	char *name = sandbox_which();

	char **names = sandbox_list();
	if (names) {
		int i;
		if (names_only) {
			for (i = 0; names[i]; ++i) { printf("%s\n", names[i]); }
		}
		else {
			for (i = 0; names[i]; ++i) {
				printf("%c %s\n",
					name && strcmp(name, names[i]) ? ' ' : '*',
					names[i]);
			}
		}
		free(names);
	}

	free(name);

	message_free();
	return !(name && names);
}
