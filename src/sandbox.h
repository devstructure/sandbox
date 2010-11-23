#ifndef SANDBOX_H
#define SANDBOX_H

int sandbox_valid(const char *name);
int sandbox_exists(const char *name, char *pathname);
int sandbox_breakout(char *name);

char **sandbox_list();
char *sandbox_which();
int sandbox_create(const char *name);
int sandbox_clone(const char *srcname, const char *destname);
int sandbox_use(const char *name, const char *command, const char *callback);
int sandbox_destroy(const char *name);

#endif
