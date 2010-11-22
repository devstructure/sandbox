#include "util.h"

#include <stdlib.h>

/* Free all elements in a null-terminated list.
 */
void util_nlist_free(void **list) {
	if (!list) { return; }
	int i;
	for (i = 0; list[i]; ++i) { free(list[i]); }
}

/* Free some elements in a null-terminated list.
 *   Free the current element to the end of the list for positive delta.
 *   Free the previous element to the beginning for negative delta.
 */
void util_nlist_free_partial(void **list, int i, int delta) {
	if (!list) { return; }
	if (0 < delta) {
		for (; list[i]; ++i) { free(list[i]); }
	}
	else if (0 > delta) {
		for (--i; i >= 0; --i) { free(list[i]); }
	}
}

/* Free all elements in an indexed list.
 */
void util_ilist_free(void **list, int ii) {
	if (!list) { return; }
	int i;
	for (i = 0; i < ii; ++i) { free(list[i]); }
}

/* Free some elements in an indexed list.
 *   Free the current element to the end of the list for positive delta.
 *   Free the previous element to the beginning for negative delta.
 */
void util_ilist_free_partial(void **list, int ii, int i, int delta) {
	if (!list) { return; }
	if (0 < delta) {
		for (; i < ii; ++i) { free(list[i]); }
	}
	else if (0 > delta) {
		for (--i; i >= 0; --i) { free(list[i]); }
	}
}

/* Free all elements in a null-terminated list of indexed lists.
 */
void util_nilist_free(void **list, int *jj) {
	if (!list) { return; }
	int i;
	for (i = 0; list[i]; ++i) {
		util_ilist_free(list[i], jj[i]);
		free(list[i]);
	}
}

/* Free some elements in a null-terminated list of indexed lists.  Only
 * the outermost list can be partially freed.  Any nested arrays that need
 * to will be completely freed.
 *   Free the current element to the end of the list for positive delta.
 *   Free the previous element to the beginning for negative delta.
 */
void util_nilist_free_partial(void **list, int *jj, int i, int delta) {
	if (!list) { return; }
	if (0 < delta) {
		for (; list[i]; ++i) {
			util_ilist_free(list[i], jj[i]);
			free(list[i]);
		}
	}
	else if (0 > delta) {
		for (--i; i >= 0; --i) {
			util_ilist_free(list[i], jj[i]);
			free(list[i]);
		}
	}
}
