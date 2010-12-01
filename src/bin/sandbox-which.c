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
		"Usage: %s [-q]\n",
		basename(argv0)
	);
}

void help() {
	fprintf(stderr,
		"  -q, --quiet operate quietly\n"
		"  -h, --help  show this help message\n"
	);
}

int main(int argc, char **argv) {
	sudo(argc, argv);
	message_init(*argv);

	const char *optstring = "qh";
	static struct option longopts[] = {
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
			case 0: /* --quiet */
				message_quiet_default(1);
				message_quiet(1);
				break;
			case 1: /* --help */
				usage(*argv);
				help();
				exit(0);
			}
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

	char *name = sandbox_which();
	if (name) {
		if (strcmp("/", name)) { printf("%s\n", name); }
		free(name);
	}

	message_free();
	return !name;
}
