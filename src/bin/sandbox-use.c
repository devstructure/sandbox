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
		"Usage: %s <sandbox> [-c <command>] [--callback=<callback>] [-q] [-h]\n",
		basename(argv0)
	);
}

void help() {
	fprintf(stderr,
		"  <sandbox>                       name of a sandbox\n"
		"-c <command>, --command=<command> command to run (defaults to your shell)\n"
		"--callback=<callback>             command to when <command> exits\n"
		"  -q, --quiet                     operate quietly\n"
		"  -h, --help                      show this help message\n"
	);
}

int main(int argc, char **argv) {
	sudo(argc, argv);
	message_init(*argv);

	char *command = 0, *callback = 0;

	const char *optstring = "c:qh";
	static struct option longopts[] = {
		{"command", 1, 0, 0},
		{"callback", 1, 0, 0},
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
			case 0: /* --command */
				command = optarg;
				break;
			case 1: /* --callback */
				callback = optarg;
				break;
			case 2: /* --quiet */
				message_quiet_default(1);
				message_quiet(1);
				break;
			case 3: /* --help */
				usage(*argv);
				help();
				exit(0);
			}
			break;
		case 'c': /* -c */
			command = optarg;
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

	int result = sandbox_use(name, command, callback);

	message_free();
	return result;
}
