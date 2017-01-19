#include "platform.h"
#include "nsync.h"
#include "time_extra.h"
#include "smprintf.h"
#include "testing.h"
#include "closure.h"
#include "time_internal.h"

NSYNC_CPP_USING_

/* --------------------------- */

/* A cv_queue represents a FIFO queue with up to limit elements.
   The storage for the queue expands as necessary up to limit. */
typedef struct cv_queue_s {
	int limit;          /* max value of count---should not be changed after initialization */
	nsync_cv non_empty; /* signalled when count transitions from zero to non-zero */
	nsync_cv non_full;  /* signalled when count transitions from limit to less than limit */
	nsync_mu mu;        /* protects fields below */
	int pos;            /* index of first in-use element */
	int count;          /* number of elements in use */
	void *data[1];      /* in use elements are data[pos, ..., (pos+count-1)%limit] */
} cv_queue;

/* Return a pointer to new cv_queue. */
static cv_queue *cv_queue_new (int limit) {
	cv_queue *q;
	int size = offsetof (struct cv_queue_s, data) + sizeof (q->data[0]) * limit;
	q = (cv_queue *) malloc (size);
	memset (q, 0, size);
	q->limit = limit;
	return (q);
}

/* Add v to the end of the FIFO *q and return non-zero, or if the FIFO already
   has limit elements and continues to do so until abs_deadline, do nothing and
   return 0. */
static int cv_queue_put (cv_queue *q, void *v, nsync_time abs_deadline) {
	int added = 0;
	nsync_mu_lock (&q->mu);
	while (q->count == q->limit &&
	       nsync_cv_wait_with_deadline (&q->non_full, &q->mu, abs_deadline, NULL) == 0) {
	}
	if (q->count != q->limit) {
		int i = q->pos + q->count;
		if (q->limit <= i) {
			i -= q->limit;
		}
		q->data[i] = v;
		if (q->count == 0) {
			nsync_cv_broadcast (&q->non_empty);
		}
		q->count++;
		added = 1;
	}
	nsync_mu_unlock (&q->mu);
	return (added);
}

/* Remove the first value from the front of the FIFO *q and return it,
   or if the FIFO is empty and continues to be so until abs_deadline,
   do nothing and return NULL. */
static void *cv_queue_get (cv_queue *q, nsync_time abs_deadline) {
	void *v = NULL;
	nsync_mu_lock (&q->mu);
	while (q->count == 0 &&
	       nsync_cv_wait_with_deadline (&q->non_empty, &q->mu, abs_deadline, NULL) == 0) {
	}
	if (q->count != 0) {
		v = q->data[q->pos];
		q->data[q->pos] = NULL;
		if (q->count == q->limit) {
			nsync_cv_broadcast (&q->non_full);
		}
		q->pos++;
		q->count--;
		if (q->pos == q->limit) {
			q->pos = 0;
		}
	}
	nsync_mu_unlock (&q->mu);
	return (v);
}

/* --------------------------- */

#define INT_TO_PTR(x) ((x) + 1 + (char *)0)
#define PTR_TO_INT(p) (((char *) (p)) - 1 - (char *)0)

/* Put count integers on *q, in the sequence start*3, (start+1)*3, (start+2)*3, .... */
static void producer_cv_n (testing t, cv_queue *q, int start, int count) {
	int i;
	for (i = 0; i != count; i++) {
		if (!cv_queue_put (q, INT_TO_PTR ((start+i)*3), nsync_time_no_deadline)) {
			TEST_FATAL (t, ("cv_queue_put() returned 0 with no deadline"));
		}
	}
}
CLOSURE_DECL_BODY4 (producer_cv_n, testing , cv_queue *, int, int)

/* Get count integers from *q, and check that they are in the
   sequence start*3, (start+1)*3, (start+2)*3, .... */
static void consumer_cv_n (testing t, cv_queue *q, int start, int count) {
	int i;
	for (i = 0; i != count; i++) {
		void *v = cv_queue_get (q, nsync_time_no_deadline);
		int x;
		if (v == NULL) {
			TEST_FATAL (t, ("cv_queue_get() returned NULL with no deadline"));
		}
		x = PTR_TO_INT (v);
		if (x != (start+i)*3) {
			TEST_FATAL (t, ("cv_queue_get() returned bad value; want %d, got %d",
				   (start+i)*3, x));
		}
	}
}

/* CV_PRODUCER_CONSUMER_N is the number of elements passed from producer to consumer in the
   test_cv_producer_consumer*() tests below. */
#define CV_PRODUCER_CONSUMER_N 100000

/* Send a stream of integers from a producer thread to
   a consumer thread via a queue with limit 10**0. */
static void test_cv_producer_consumer0 (testing t) {
	cv_queue *q = cv_queue_new (1);
	closure_fork (closure_producer_cv_n (&producer_cv_n, t, q, 0, CV_PRODUCER_CONSUMER_N));
	consumer_cv_n (t, q, 0, CV_PRODUCER_CONSUMER_N);
	free (q);
}

/* Send a stream of integers from a producer thread to
   a consumer thread via a queue with limit 10**1. */
static void test_cv_producer_consumer1 (testing t) {
	cv_queue *q = cv_queue_new (10);
	closure_fork (closure_producer_cv_n (&producer_cv_n, t, q, 0, CV_PRODUCER_CONSUMER_N));
	consumer_cv_n (t, q, 0, CV_PRODUCER_CONSUMER_N);
	free (q);
}

/* Send a stream of integers from a producer thread to
   a consumer thread via a queue with limit 10**2. */
static void test_cv_producer_consumer2 (testing t) {
	cv_queue *q = cv_queue_new (100);
	closure_fork (closure_producer_cv_n (&producer_cv_n, t, q, 0, CV_PRODUCER_CONSUMER_N));
	consumer_cv_n (t, q, 0, CV_PRODUCER_CONSUMER_N);
	free (q);
}

/* Send a stream of integers from a producer thread to
   a consumer thread via a queue with limit 10**3. */
static void test_cv_producer_consumer3 (testing t) {
	cv_queue *q = cv_queue_new (1000);
	closure_fork (closure_producer_cv_n (&producer_cv_n, t, q, 0, CV_PRODUCER_CONSUMER_N));
	consumer_cv_n (t, q, 0, CV_PRODUCER_CONSUMER_N);
	free (q);
}

/* Send a stream of integers from a producer thread to
   a consumer thread via a queue with limit 10**4. */
static void test_cv_producer_consumer4 (testing t) {
	cv_queue *q = cv_queue_new (10 * 1000);
	closure_fork (closure_producer_cv_n (&producer_cv_n, t, q, 0, CV_PRODUCER_CONSUMER_N));
	consumer_cv_n (t, q, 0, CV_PRODUCER_CONSUMER_N);
	free (q);
}

/* Send a stream of integers from a producer thread to
   a consumer thread via a queue with limit 10**5. */
static void test_cv_producer_consumer5 (testing t) {
	cv_queue *q = cv_queue_new (100 * 1000);
	closure_fork (closure_producer_cv_n (&producer_cv_n, t, q, 0, CV_PRODUCER_CONSUMER_N));
	consumer_cv_n (t, q, 0, CV_PRODUCER_CONSUMER_N);
	free (q);
}

/* Send a stream of integers from a producer thread to
   a consumer thread via a queue with limit 10**6. */
static void test_cv_producer_consumer6 (testing t) {
	cv_queue *q = cv_queue_new (1000 * 1000);
	closure_fork (closure_producer_cv_n (&producer_cv_n, t, q, 0, CV_PRODUCER_CONSUMER_N));
	consumer_cv_n (t, q, 0, CV_PRODUCER_CONSUMER_N);
	free (q);
}

/* The following values control how aggressively we police the timeout. */
#define TOO_EARLY_MS 1
#define TOO_LATE_MS 100   /* longer, to accommodate scheduling delays */
#define TOO_LATE_ALLOWED 25         /* number of iterations permitted to violate too_late */

/* Check timeouts on a CV wait_with_deadline(). */
static void test_cv_deadline (testing t) {
	int too_late_violations;
	nsync_mu mu = NSYNC_MU_INIT;
	nsync_cv cv = NSYNC_CV_INIT;
	int i;
	nsync_time too_early;
	nsync_time too_late;

	too_early = nsync_time_ms (TOO_EARLY_MS);
	too_late = nsync_time_ms (TOO_LATE_MS);
	too_late_violations = 0;
	nsync_mu_lock (&mu);
	for (i = 0; i != 50; i++) {
		nsync_time end_time;
		nsync_time start_time;
		nsync_time expected_end_time;
		start_time = nsync_time_now ();
		expected_end_time = nsync_time_add (start_time, nsync_time_ms (87));
		if (nsync_cv_wait_with_deadline (&cv, &mu, expected_end_time,
						 NULL) != ETIMEDOUT) {
			TEST_FATAL (t, ("nsync_cv_wait() returned non-expired for a timeout"));
		}
		end_time = nsync_time_now ();
		if (nsync_time_cmp (end_time, nsync_time_sub (expected_end_time, too_early)) < 0) {
			char *elapsed_str = nsync_time_str (nsync_time_sub (expected_end_time, end_time), 2);
			TEST_ERROR (t, ("nsync_cv_wait() returned %s too early", elapsed_str));
			free (elapsed_str);
		}
		if (nsync_time_cmp (nsync_time_add (expected_end_time, too_late), end_time) < 0) {
			too_late_violations++;
		}
	}
	nsync_mu_unlock (&mu);
	if (too_late_violations > TOO_LATE_ALLOWED) {
		TEST_ERROR (t, ("nsync_cv_wait() returned too late %d times", too_late_violations));
	}
}

/* Check cancellations with nsync_cv_wait_with_deadline(). */
static void test_cv_cancel (testing t) {
	nsync_time future_time;
	int too_late_violations;
	nsync_mu mu = NSYNC_MU_INIT;
	nsync_cv cv = NSYNC_CV_INIT;
	int i;
	nsync_time too_early;
	nsync_time too_late;

	too_early = nsync_time_ms (TOO_EARLY_MS);
	too_late = nsync_time_ms (TOO_LATE_MS);

	/* The loops below cancel after 87 milliseconds, like the timeout tests above. */

	future_time = nsync_time_add (nsync_time_now (), nsync_time_ms (3600000)); /* test cancels with timeout */

	too_late_violations = 0;
	nsync_mu_lock (&mu);
	for (i = 0; i != 50; i++) {
		int x;
		nsync_note cancel;
		nsync_time end_time;
		nsync_time start_time;
		nsync_time expected_end_time;
		start_time = nsync_time_now ();
		expected_end_time = nsync_time_add (start_time, nsync_time_ms (87));

		nsync_note_init (&cancel, NULL, expected_end_time);

		x = nsync_cv_wait_with_deadline (&cv, &mu, future_time, &cancel);
		if (x != ECANCELED) {
			TEST_FATAL (t, ("nsync_cv_wait() returned non-cancelled (%d) for "
				   "a cancellation; expected %d",
				   x, ECANCELED));
		}
		end_time = nsync_time_now ();
		if (nsync_time_cmp (end_time, nsync_time_sub (expected_end_time, too_early)) < 0) {
			char *elapsed_str = nsync_time_str (nsync_time_sub (expected_end_time, end_time), 2);
			TEST_ERROR (t, ("nsync_cv_wait() returned %s too early", elapsed_str));
			free (elapsed_str);
		}
		if (nsync_time_cmp (nsync_time_add (expected_end_time, too_late), end_time) < 0) {
			too_late_violations++;
		}

		/* Check that an already cancelled wait returns immediately. */
		start_time = nsync_time_now ();

		x = nsync_cv_wait_with_deadline (&cv, &mu, nsync_time_no_deadline, &cancel);
		if (x != ECANCELED) {
			TEST_FATAL (t, ("nsync_cv_wait() returned non-cancelled (%d) for "
				   "a cancellation; expected %d",
				   x, ECANCELED));
		}
		end_time = nsync_time_now ();
		if (nsync_time_cmp (end_time, start_time) < 0) {
			char *elapsed_str = nsync_time_str (nsync_time_sub (expected_end_time, end_time), 2);
			TEST_ERROR (t, ("nsync_cv_wait() returned %s too early", elapsed_str));
			free (elapsed_str);
		}
		if (nsync_time_cmp (nsync_time_add (start_time, too_late), end_time) < 0) {
			too_late_violations++;
		}
		nsync_note_notify (&cancel);
	}
	nsync_mu_unlock (&mu);
	if (too_late_violations > TOO_LATE_ALLOWED) {
		TEST_ERROR (t, ("nsync_cv_wait() returned too late %d times", too_late_violations));
	}
}

int main (int argc, char *argv[]) {
	testing_base tb = testing_new (argc, argv, 0);
	TEST_RUN (tb, test_cv_producer_consumer0);
	TEST_RUN (tb, test_cv_producer_consumer1);
	TEST_RUN (tb, test_cv_producer_consumer2);
	TEST_RUN (tb, test_cv_producer_consumer3);
	TEST_RUN (tb, test_cv_producer_consumer4);
	TEST_RUN (tb, test_cv_producer_consumer5);
	TEST_RUN (tb, test_cv_producer_consumer6);
	TEST_RUN (tb, test_cv_deadline);
	TEST_RUN (tb, test_cv_cancel);
	return (testing_base_exit (tb));
}
