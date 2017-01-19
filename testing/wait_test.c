#include "platform.h"
#include "nsync.h"
#include "time_extra.h"
#include "smprintf.h"
#include "closure.h"
#include "testing.h"
#include "array.h"
#include "time_internal.h"

NSYNC_CPP_USING_

static void decrement_at (nsync_counter *c, nsync_time abs_deadline, nsync_counter *done) {
	nsync_time_sleep_until (abs_deadline);
	nsync_counter_add (c, -1);
	nsync_counter_add (done, -1);
}

CLOSURE_DECL_BODY3 (decrement, nsync_counter *, nsync_time, nsync_counter *)

static void notify_at (nsync_note *n, nsync_time abs_deadline, nsync_counter *done) {
	nsync_time_sleep_until (abs_deadline);
	nsync_note_notify (n);
	nsync_counter_add (done, -1);
}

CLOSURE_DECL_BODY3 (notify, nsync_note *, nsync_time, nsync_counter *)

typedef A_TYPE (struct nsync_waitable_s) a_waitable;
typedef A_TYPE (struct nsync_waitable_s *) a_pwaitable;

/* Test nsync_wait_n(). */
static void test_wait_n (testing t) {
	int i;
	int j;
	int k;
	int ncounter = 10;
	int nnote = 10;
	int nnote_expire = 10;
	for (i = 0; i != 30; i++) {
		nsync_counter *done = (nsync_counter *) malloc (sizeof (*done));
		nsync_time now;
		nsync_time deadline;
		a_waitable aw;
		a_pwaitable apw;
		memset (&aw, 0, sizeof (aw));
		memset (&apw, 0, sizeof (apw));
		now = nsync_time_now ();
		deadline = nsync_time_add (now, nsync_time_ms (100));
		nsync_counter_init (done, 0);
		for (j = A_LEN (&aw); A_LEN (&aw) < j+ncounter;) {
			nsync_counter *c = (nsync_counter *) malloc (sizeof (*c));
			struct nsync_waitable_s *w = &A_PUSH (&aw);
			w->v = c;
			w->funcs = &nsync_counter_waitable_funcs;
			nsync_counter_init (c, 0);
			for (k = 0; k != 4 && A_LEN (&aw) < j+ncounter; k++) {
				nsync_counter_add (c, 1);
				nsync_counter_add (done, 1);
				closure_fork (closure_decrement (&decrement_at, c, deadline, done));
			}
		}
		for (j = A_LEN (&aw); A_LEN (&aw) < j+nnote;) {
			nsync_note *n = (nsync_note *) malloc (sizeof (*n));
			struct nsync_waitable_s *w = &A_PUSH (&aw);
			w->v = n;
			w->funcs = &nsync_note_waitable_funcs;
			nsync_note_init (n, NULL, nsync_time_no_deadline);
			nsync_counter_add (done, 1);
			closure_fork (closure_notify (&notify_at, n, deadline, done));
			for (k = 0; k != 4 && A_LEN (&aw) < j+nnote; k++) {
				nsync_note *cn = (nsync_note *) malloc (sizeof (*cn));
				struct nsync_waitable_s *w = &A_PUSH (&aw);
				w->v = cn;
				w->funcs = &nsync_note_waitable_funcs;
				nsync_note_init (cn, n, nsync_time_no_deadline);
			}
		}
		for (j = A_LEN (&aw); A_LEN (&aw) < j+nnote_expire;) {
			nsync_note *n = (nsync_note *) malloc (sizeof (*n));
			struct nsync_waitable_s *w = &A_PUSH (&aw);
			w->v = n;
			w->funcs = &nsync_note_waitable_funcs;
			nsync_note_init (n, NULL, deadline);
			nsync_counter_add (done, 1);
			closure_fork (closure_notify (&notify_at, n, deadline, done));
			for (k = 0; k != 4 && A_LEN (&aw) < j+nnote; k++) {
				nsync_note *cn = (nsync_note *) malloc (sizeof (*cn));
				struct nsync_waitable_s *w = &A_PUSH (&aw);
				w->v = cn;
				w->funcs = &nsync_note_waitable_funcs;
				nsync_note_init (cn, n, nsync_time_no_deadline);
			}
		}
		if (ncounter + nnote + nnote_expire != A_LEN (&aw)) {
			TEST_ERROR (t, ("array length not equal to number of counters"));
		}
		for (j = 0; j != A_LEN (&aw); j++) {
			A_PUSH (&apw) = &A (&aw, j);
		}
		while (A_LEN (&apw) != 0) {
			k = nsync_wait_n (NULL, NULL, NULL, nsync_time_no_deadline,
					  A_LEN (&apw), &A (&apw, 0));
			if (k == A_LEN (&apw)) {
				TEST_ERROR (t, ("nsync_wait_n returned with no waiter ready"));
			}
			A (&apw, k) = A (&apw, A_LEN (&apw) - 1);
			A_DISCARD (&apw, 1);
		}
		nsync_counter_wait (done, nsync_time_no_deadline);
		for (k = 0; k != A_LEN (&aw); k++) {
			free (A (&aw, k).v);
		}
		A_FREE (&apw);
		A_FREE (&aw);
		free (done);
	}
}

int main (int argc, char *argv[]) {
	testing_base tb = testing_new (argc, argv, 0);
	TEST_RUN (tb, test_wait_n);
	return (testing_base_exit (tb));
}
