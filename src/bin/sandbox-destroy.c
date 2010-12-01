#include "../message.h"
#include "../sandbox.h"
#include "../sudo.h"

#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void usage(char *argv0) {
	fprintf(stderr,
		"Usage: %s [-q] <name>\n",
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
	char *name;
	switch (argc - optind) {
	case 1:
		name = argv[optind];
		break;
	default:
		usage(*argv);
		exit(1);
		break;
	}
	if (!sandbox_valid(name)) {
		message_loud("invalid sandbox name %s\n", name);
		exit(1);
	}

	int result = sandbox_destroy(name);

	message_free();
	return result;
}
