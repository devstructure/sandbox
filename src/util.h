#ifndef UTIL_H
#define UTIL_H

void util_nlist_free(void **list);
void util_nlist_free_partial(void **list, int i, int delta);
void util_ilist_free(void **list, int ii);
void util_ilist_free_partial(void **list, int ii, int i, int delta);
void util_nilist_free(void **list, int *jj);
void util_nilist_free_partial(void **list, int *jj, int i, int delta);

#endif
