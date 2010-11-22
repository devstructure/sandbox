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
		"Usage: %s [<sandbox-src>] <sandbox-dest> [-q] [-h]\n",
		basename(argv0)
	);
}

void help() {
	fprintf(stderr,
		"  <sandbox-src>  name of an existing sandbox\n"
		"  <sandbox-dest> name of a nonexistent sandbox\n"
		"  -q, --quiet    operate quietly\n"
		"  -h, --help     show this help message\n"
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
	char *srcname, *destname;
	switch (argc - optind) {
	case 2:
		srcname = argv[optind];
		destname = argv[optind + 1];
		break;
	case 1:
		srcname = 0;
		destname = argv[optind];
		break;
	default:
		usage(*argv);
		exit(1);
		break;
	}
	if (!sandbox_valid(srcname)) {
		message_loud("invalid sandbox name %s\n", srcname);
		exit(1);
	}
	if (!sandbox_valid(destname)) {
		message_loud("invalid sandbox name %s\n", destname);
		exit(1);
	}

	int result = sandbox_clone(srcname, destname);

	message_free();
	return result;
}
