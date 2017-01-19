#include "nsync_cpp.h"
#include "platform.h"
#include "compiler.h"
#include "cputype.h"
#include "dll.h"

NSYNC_CPP_START_

/* Initialize *e. */
void nsync_dll_init_ (nsync_dll_element_ *e, void *container) {
	e->next = e;
	e->prev = e;
	e->container = container;
}

/* Return whether list is empty. */
int nsync_dll_is_empty_ (nsync_dll_list_ list) {
	return (list == NULL);
}

/* Remove *e from list, and returns the new list. */
nsync_dll_list_ nsync_dll_remove_ (nsync_dll_list_ list, nsync_dll_element_ *e) {
	if (list == e) { /* removing tail of list */
		if (list->prev == list) {
			list = NULL; /* removing only element of list */
		} else {
			list = list->prev;
		}
	}
	e->next->prev = e->prev;
	e->prev->next = e->next;
	e->next = e;
	e->prev = e;
	return (list);
}

/* Cause element *n and its successors to come after element *p.
   Requires n and p are non-NULL and do not point at elements of the same list. */
void nsync_dll_splice_after_ (nsync_dll_element_ *p, nsync_dll_element_ *n) {
	nsync_dll_element_ *tmp = p->next;
	p->next = n;
	n->prev->next = tmp;
	tmp->prev = n->prev;
	n->prev = p;
}

/* Make element *e the first element of list, and return
   the list.  The resulting list will have *e as its first element, followed by
   any elements in the same list as *e, followed by the elements that were
   previously in list.  Requires that *e not be in list.  If e==NULL, list is
   returned unchanged. */
nsync_dll_list_ nsync_dll_make_first_in_list_ (nsync_dll_list_ list, nsync_dll_element_ *e) {
	if (e != NULL) {
		if (list == NULL) {
			list = e->prev;
		} else {
			nsync_dll_element_ *first_in_old_list = list->next;
			list->next = e;
			e->prev->next = first_in_old_list;
			first_in_old_list->prev = e->prev;
			e->prev = list;
		}
	}
	return (list);
}

/* Make element *e the last element of list, and return
   the list.  The resulting list will have *e as its last element, preceded by
   any elements in the same list as *e, preceded by the elements that were
   previously in list.  Requires that *e not be in list.  If e==NULL, list is
   returned unchanged. */
nsync_dll_list_ nsync_dll_make_last_in_list_ (nsync_dll_list_ list, nsync_dll_element_ *e) {
	if (e != NULL) {
		nsync_dll_make_first_in_list_ (list, e->next);
		list = e;
	}
	return (list);
}

/* Return a pointer to the first element of list, or NULL if list is empty. */
nsync_dll_element_ *nsync_dll_first_ (nsync_dll_list_ list) {
	nsync_dll_element_ *first = NULL;
	if (list != NULL) {
		first = list->next;
	}
	return (first);
}

/* Return a pointer to the last element of list, or NULL if list is empty. */
nsync_dll_element_ *nsync_dll_last_ (nsync_dll_list_ list) {
	return (list);
}

/* Return a pointer to the next element of list following *e,
   or NULL if there is no such element. */
nsync_dll_element_ *nsync_dll_next_ (nsync_dll_list_ list, nsync_dll_element_ *e) {
	nsync_dll_element_ *next = NULL;
	if (e != list) {
		next = e->next;
	}
	return (next);
}

/* Return a pointer to the previous element of list following *e,
   or NULL if there is no such element. */
nsync_dll_element_ *nsync_dll_prev_ (nsync_dll_list_ list, nsync_dll_element_ *e) {
	nsync_dll_element_ *prev = NULL;
	if (e != list->next) {
		prev = e->prev;
	}
	return (prev);
}

NSYNC_CPP_END_
