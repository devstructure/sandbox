#ifndef SERVICES_H
#define SERVICES_H

#include <dirent.h>
#include <glib.h>

GHashTable *services_list(
	const char **dirnames, struct dirent ***namelists, int *jj
);
int services_stop(const char **dirnames, GHashTable *services);

#endif
