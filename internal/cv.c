#include "nsync_cpp.h"
#include "platform.h"
#include "compiler.h"
#include "cputype.h"
#include "nsync.h"
#include "dll.h"
#include "sem.h"
#include "common.h"
#include "atomic.h"
#include "time_internal.h"

NSYNC_CPP_START_

/* Bits in nsync_cv.word */

#define CV_SPINLOCK ((uint32_t) (1 << 0)) /* protects waiters */
#define CV_NON_EMPTY ((uint32_t) (1 << 1)) /* waiters list is non-empty */

static struct nsync_waiter_s *assert_nwq_size =
	(struct nsync_waiter_s *)(uintptr_t)(1 /
	(sizeof (assert_nwq_size->q) >= sizeof (nsync_dll_element_)));

/* Wake the cv waiters in the circular list pointed to by
   to_wake_list, which may not be NULL.  If the waiter is associated with a
   nsync_mu, the "wakeup" may consist of transferring the waiters to the nsync_mu's
   queue.  Requires that every waiter is associated with the same mutex.
   all_readers indicates whether all the waiters on the list are readers.  */
static void wake_waiters (nsync_dll_list_ to_wake_list, int all_readers) {
	nsync_dll_element_ *p = NULL;
	nsync_dll_element_ *next = NULL;
	nsync_dll_element_ *first_waiter = nsync_dll_first_ (to_wake_list);
	struct nsync_waiter_s *first_nw = DLL_NSYNC_WAITER (first_waiter);
	waiter *first_w = NULL;
	nsync_mu *pmu = NULL;
	if ((first_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0) {
		first_w = DLL_WAITER (first_waiter);
		pmu = first_w->cv_mu;
	}
	if (pmu != NULL) { /* waiter is associated with the nsync_mu *pmu. */
		/* We will transfer elements of to_wake_list to *pmu if all of:
		    - *pmu's spinlock is not held, and
		    - either *pmu cannot be acquired in the mode of the first
		      waiter, or there's more than one thread on to_wake_list
		      and not all are readers, and
		    - we acquire the spinlock on the first try.
		   The spinlock acquisition also marks *pmu as having waiters.
		   */
		uint32_t old_mu_word = ATM_LOAD (&pmu->word);
		int first_cant_acquire = ((old_mu_word & first_w->l_type->zero_to_acquire) != 0);
		/* set_desig_waker==MU_DESIG_WAKER if a thread is to be woken
		   rather than transferred */
		uint32_t set_desig_waker = 0;
		if (!first_cant_acquire) {
			set_desig_waker = MU_DESIG_WAKER;
		}
		next = nsync_dll_next_ (to_wake_list, first_waiter);
		if ((old_mu_word&MU_SPINLOCK) == 0 &&
		    (first_cant_acquire || (first_waiter != next && !all_readers)) &&
		    ATM_CAS_ACQ (&pmu->word, old_mu_word,
				 (old_mu_word|MU_SPINLOCK|MU_WAITING|set_desig_waker) &
				 ~MU_ALL_FALSE)) {

			uint32_t set_on_release = 0;

			/* For any waiter that should be transferred, rather
			   than woken, move it from to_wake_list to pmu->waiters. */
			int first_is_writer = first_w->l_type == nsync_writer_type_;
			int transferred_a_writer = 0;
			int woke_areader = 0;
			/* Transfer the first waiter iff it can't acquire *pmu. */
			if (first_cant_acquire) {
				to_wake_list = nsync_dll_remove_ (to_wake_list, first_waiter);
				pmu->waiters = nsync_dll_make_last_in_list_ (pmu->waiters, first_waiter);
				/* tell nsync_cv_wait_with_deadline() that we
				   moved the waiter to *pmu's queue.  */
				first_w->cv_mu = NULL;
				/* first_nw.waiting is already 1, from being on
				   cv's waiter queue.  */
				transferred_a_writer = first_is_writer;
			} else {
				woke_areader = !first_is_writer;
			}
			/* Now process the other waiters. */
			for (p = next; p != NULL; p = next) {
				int p_is_writer;
				struct nsync_waiter_s *p_nw = DLL_NSYNC_WAITER (p);
				waiter *p_w = NULL;
				if ((p_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0) {
					p_w = DLL_WAITER (p);
				}
				next = nsync_dll_next_ (to_wake_list, p);
				p_is_writer = (p_w != NULL &&
					       DLL_WAITER (p)->l_type == nsync_writer_type_);
				/* We transfer this element if any of:
				   - the first waiter can't acquire *pmu, or
				   - the first waiter is a writer, or
				   - this element is a writer. */
				if (p_w == NULL) {
					/* wake non-native waiter */
				} else if (first_cant_acquire || first_is_writer || p_is_writer) {
					to_wake_list = nsync_dll_remove_ (to_wake_list, p);
					pmu->waiters = nsync_dll_make_last_in_list_ (pmu->waiters, p);
					/* tell nsync_cv_wait_with_deadline()
					   that we moved the waiter to *pmu's
					   queue.  */
					p_w->cv_mu = NULL;
					/* p_nw->waiting is already 1, from
					   being on cv's waiter queue.  */
					transferred_a_writer = transferred_a_writer || p_is_writer;
				} else {
					woke_areader = woke_areader || !p_is_writer;
				}
			}

			/* Claim a waiting writer if we transferred one, except if we woke readers,
			   in which case we want those readers to be able to acquire immediately. */
			if (transferred_a_writer && !woke_areader) {
				set_on_release |= MU_WRITER_WAITING;
			}

			/* release *pmu's spinlock  (MU_WAITING was set by CAS above) */
			old_mu_word = ATM_LOAD (&pmu->word);
			while (!ATM_CAS_REL (&pmu->word, old_mu_word,
					     (old_mu_word|set_on_release) & ~MU_SPINLOCK)) {
				old_mu_word = ATM_LOAD (&pmu->word);
			}
		} else if ((old_mu_word & (MU_SPINLOCK | MU_ANY_LOCK | MU_DESIG_WAKER)) == 0) {
			/* If spinlock and lock not held, try to set MU_DESIG_WAKER because at
			   least one thread is to be woken.  An optimization; ignore failures. */
			ATM_CAS_RELACQ (&pmu->word, old_mu_word,
					old_mu_word|MU_DESIG_WAKER);
		}
	}

	/* Wake any waiters we didn't manage to enqueue on the mu. */
	for (p = nsync_dll_first_ (to_wake_list); p != NULL; p = next) {
		struct nsync_waiter_s *p_nw = DLL_NSYNC_WAITER (p);
		next = nsync_dll_next_ (to_wake_list, p);
		to_wake_list = nsync_dll_remove_ (to_wake_list, p);
		/* Wake the waiter. */
		ATM_STORE_REL (&p_nw->waiting, 0); /* release store */
		nsync_mu_semaphore_v (p_nw->sem);
	}
}

/* ------------------------------------------ */

/* Atomically release *pmu (which must be held on entry)
   and block the calling thread on *pcv.  Then wait until awakened by a
   call to nsync_cv_signal() or nsync_cv_broadcast() (or a spurious wakeup), or by the time
   reaching abs_deadline, or by cancel_note being notified.  In all cases,
   reacquire *pmu, and return the reason for the call returned (0, ETIMEDOUT,
   or ECANCELED).  Callers should abs_deadline==nsync_time_no_deadline for no
   deadline, and cancel_note==NULL for no cancellation.  nsync_cv_wait_with_deadline()
   should be used in a loop, as with all Mesa-style condition variables.  See
   examples above.

   There are two reasons for using an absolute deadline, rather than a relative
   timeout---these are why pthread_cond_timedwait() also uses an absolute
   deadline.  First, condition variable waits have to be used in a loop; with
   an absolute times, the deadline does not have to be recomputed on each
   iteration.  Second, in most real programmes, some activity (such as an RPC
   to a server, or when guaranteeing response time in a UI), there is a
   deadline imposed by the specification or the caller/user; relative delays
   can shift arbitrarily with scheduling delays, and so after multiple waits
   might extend beyond the expected deadline.  Relative delays tend to be more
   convenient mostly in tests and trivial examples than they are in real
   programmes. */
int nsync_cv_wait_with_deadline_generic (nsync_cv *pcv, void *pmu,
					 void (*lock) (void *), void (*unlock) (void *),
					 nsync_time abs_deadline,
					 nsync_note *cancel_note) {
	nsync_mu *cv_mu = NULL;
	int is_reader_mu;
	uint32_t old_word;
	uint32_t remove_count;
	int sem_outcome;
	unsigned attempts;
	int outcome = 0;
	waiter *w;
	IGNORE_RACES_START ();
	w = nsync_waiter_new_ ();
	ATM_STORE (&w->nw.waiting, 1);
	w->cond.f = NULL; /* Not using a conditional critical section. */
	w->cond.v = NULL;
	if (lock == (void (*) (void *)) & nsync_mu_lock ||
	    lock == (void (*) (void *)) & nsync_mu_rlock) {
		cv_mu = (nsync_mu *) pmu;
	}
	w->cv_mu = cv_mu;       /* If *pmu is an nsync_mu, record its address, else record NULL. */
	is_reader_mu = 0; /* If true, an nsync_mu in reader mode. */
	if (cv_mu == NULL) {
		w->l_type = NULL;
	} else {
		uint32_t old_mu_word = ATM_LOAD (&cv_mu->word);
		int is_writer = (old_mu_word & MU_WHELD_IF_NON_ZERO) != 0;
		int is_reader = (old_mu_word & MU_RHELD_IF_NON_ZERO) != 0;
		if (is_writer) {
			if (is_reader) {
				nsync_panic_ ("mu held in reader and writer mode simultaneously "
				       "on entry to nsync_cv_wait_with_deadline()\n");
			}
			w->l_type = nsync_writer_type_;
		} else if (is_reader) {
			w->l_type = nsync_reader_type_;
			is_reader_mu = 1;
		} else {
			nsync_panic_ ("mu not held on entry to nsync_cv_wait_with_deadline()\n");
		}
	}

	/* acquire spinlock, set non-empty */
	old_word = nsync_spin_test_and_set_ (&pcv->word, CV_SPINLOCK, CV_SPINLOCK|CV_NON_EMPTY, 0);
	pcv->waiters = nsync_dll_make_last_in_list_ (pcv->waiters, (nsync_dll_element_ *)&w->nw.q);
	remove_count = ATM_LOAD (&w->remove_count);
	/* Release the spin lock. */
	ATM_STORE_REL (&pcv->word, old_word|CV_NON_EMPTY); /* release store */

	/* Release *pmu. */
	if (is_reader_mu) {
		nsync_mu_runlock (cv_mu);
	} else {
		(*unlock) (pmu);
	}

	/* wait until awoken or a timeout. */
	sem_outcome = 0;
	attempts = 0;
	while (ATM_LOAD_ACQ (&w->nw.waiting) != 0) { /* acquire load */
		if (sem_outcome == 0) {
			sem_outcome = nsync_sem_wait_with_cancel_ (w, abs_deadline, cancel_note);
		}

		if (sem_outcome != 0 && ATM_LOAD (&w->nw.waiting) != 0) {
			/* A timeout or cancellation occurred, and no wakeup.
			   Acquire *pcv's spinlock, and confirm.  */
			old_word = nsync_spin_test_and_set_ (&pcv->word, CV_SPINLOCK,
							     CV_SPINLOCK, 0);
			/* Check that w wasn't removed from the queue after we
			   checked above, but before we acquired the spinlock.
			   The test of remove_count confirms that the waiter *w
			   is still governed by *pcv's spinlock; otherwise, some
			   other thread is about to set w.waiting==0.  */
			if (ATM_LOAD (&w->nw.waiting) != 0) {
				if (remove_count == ATM_LOAD (&w->remove_count)) {
					uint32_t old_value;
					/* still in cv waiter queue */
					/* Not woken, so remove *w from cv
					   queue, and declare a
					   timeout/cancellation.  */
					outcome = sem_outcome;
					pcv->waiters = nsync_dll_remove_ (pcv->waiters,
								  (nsync_dll_element_ *)&w->nw.q);
					do {    
						old_value = ATM_LOAD (&w->remove_count);
					} while (!ATM_CAS (&w->remove_count, old_value, old_value+1));
					if (nsync_dll_is_empty_ (pcv->waiters)) {
						old_word &= ~(CV_NON_EMPTY);
					}
					ATM_STORE_REL (&w->nw.waiting, 0); /* release store */
				}
			}
			/* Release spinlock. */
			ATM_STORE_REL (&pcv->word, old_word); /* release store */
		}

		if (ATM_LOAD (&w->nw.waiting) != 0) {
			attempts = nsync_spin_delay_ (attempts); /* so we will ultimately yield. */
		}
	}

	if (cv_mu != NULL && w->cv_mu == NULL) { /* waiter was moved to *pmu's queue, and woken. */
		/* Requeue on *pmu using existing waiter struct; current thread
		   is the designated waker.  */
		nsync_mu_lock_slow_ (cv_mu, w, MU_DESIG_WAKER, w->l_type);
		nsync_waiter_free_ (w);
	} else {
		/* Traditional case: We've woken from the cv, and need to reacquire *pmu. */
		nsync_waiter_free_ (w);
		if (is_reader_mu) {
			nsync_mu_rlock (cv_mu);
		} else {
			(*lock) (pmu);
		}
	}
	IGNORE_RACES_END ();
	return (outcome);
}

/* Wake at least one thread if any are currently blocked on *pcv.  If
   the chosen thread is a reader on an nsync_mu, wake all readers and, if
   possible, a writer. */
void nsync_cv_signal (nsync_cv *pcv) {
	IGNORE_RACES_START ();
	if ((ATM_LOAD_ACQ (&pcv->word) & CV_NON_EMPTY) != 0) { /* acquire load */
		nsync_dll_list_ to_wake_list = NULL; /* waiters that we will wake */
		int all_readers = 0;
		/* acquire spinlock */
		uint32_t old_word = nsync_spin_test_and_set_ (&pcv->word, CV_SPINLOCK,
							      CV_SPINLOCK, 0);
		if (!nsync_dll_is_empty_ (pcv->waiters)) {
			/* Point to first waiter that enqueued itself, and
			   detach it from all others.  */
			struct nsync_waiter_s *first_nw;
			nsync_dll_element_ *first = nsync_dll_first_ (pcv->waiters);
			pcv->waiters = nsync_dll_remove_ (pcv->waiters, first);
			first_nw = DLL_NSYNC_WAITER (first);
			if ((first_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0) {
				uint32_t old_value;
				do {    
					old_value =
						ATM_LOAD (&DLL_WAITER (first)->remove_count);
				} while (!ATM_CAS (&DLL_WAITER (first)->remove_count,
						   old_value, old_value+1));
			}
			to_wake_list = nsync_dll_make_last_in_list_ (to_wake_list, first);
			if ((first_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0 &&
			    DLL_WAITER (first)->l_type == nsync_reader_type_) {
				int woke_writer;
				/* If the first waiter is a reader, wake all readers, and
				   if it's possible, one writer. */
				nsync_dll_element_ *p = NULL;
				nsync_dll_element_ *next = NULL;
				all_readers = 1;
				woke_writer = 0;
				for (p = nsync_dll_first_ (pcv->waiters); p != NULL; p = next) {
					struct nsync_waiter_s *p_nw = DLL_NSYNC_WAITER (p);
					int should_wake;
					next = nsync_dll_next_ (pcv->waiters, p);
					should_wake = 0;
					if ((p_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0 &&
					     DLL_WAITER (p)->l_type == nsync_reader_type_) {
						should_wake = 1;
					} else if (!woke_writer) {
						woke_writer = 1;
						all_readers = 0;
						should_wake = 1;
					}
					if (should_wake) {
						pcv->waiters = nsync_dll_remove_ (pcv->waiters, p);
						if ((p_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0) {
							uint32_t old_value;
							do {    
								old_value = ATM_LOAD (
								    &DLL_WAITER (p)->remove_count);
							} while (!ATM_CAS (&DLL_WAITER (p)->remove_count,
									   old_value, old_value+1));
						}
						to_wake_list = nsync_dll_make_last_in_list_ (
							to_wake_list, p);
					}
				}
			}
			if (nsync_dll_is_empty_ (pcv->waiters)) {
				old_word &= ~(CV_NON_EMPTY);
			}
		}
		/* Release spinlock. */
		ATM_STORE_REL (&pcv->word, old_word); /* release store */
		if (!nsync_dll_is_empty_ (to_wake_list)) {
			wake_waiters (to_wake_list, all_readers);
		}
	}
	IGNORE_RACES_END ();
}

/* Wake all threads currently blocked on *pcv. */
void nsync_cv_broadcast (nsync_cv *pcv) {
	IGNORE_RACES_START ();
	if ((ATM_LOAD_ACQ (&pcv->word) & CV_NON_EMPTY) != 0) { /* acquire load */
		nsync_dll_element_ *p;
		nsync_dll_element_ *next;
		int all_readers;
		nsync_dll_list_ to_wake_list = NULL;   /* waiters that we will wake */
		/* acquire spinlock */
		nsync_spin_test_and_set_ (&pcv->word, CV_SPINLOCK, CV_SPINLOCK, 0);
		p = NULL;
		next = NULL;
		all_readers = 1;
		/* Wake entire waiter list, which we leave empty. */
		for (p = nsync_dll_first_ (pcv->waiters); p != NULL; p = next) {
			struct nsync_waiter_s *p_nw = DLL_NSYNC_WAITER (p);
			next = nsync_dll_next_ (pcv->waiters, p);
			all_readers = all_readers && (p_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0 &&
				      (DLL_WAITER (p)->l_type == nsync_reader_type_);
			pcv->waiters = nsync_dll_remove_ (pcv->waiters, p);
			if ((p_nw->flags & NSYNC_WAITER_FLAG_MUCV) != 0) {
				uint32_t old_value;
				do {    
					old_value = ATM_LOAD (&DLL_WAITER (p)->remove_count);
				} while (!ATM_CAS (&DLL_WAITER (p)->remove_count,
						   old_value, old_value+1));
			}
			to_wake_list = nsync_dll_make_last_in_list_ (to_wake_list, p);
		}
		/* Release spinlock and mark queue empty. */
		ATM_STORE_REL (&pcv->word, 0); /* release store */
		if (!nsync_dll_is_empty_ (to_wake_list)) {    /* Wake them. */
			wake_waiters (to_wake_list, all_readers);
		}
	}
	IGNORE_RACES_END ();
}

/* Wait with deadline, using an nsync_mu. */
int nsync_cv_wait_with_deadline (nsync_cv *pcv, nsync_mu *pmu,
				 nsync_time abs_deadline,
				 nsync_note *cancel_note) {
	return (nsync_cv_wait_with_deadline_generic (pcv, pmu, (void (*) (void *)) &nsync_mu_lock,
						     (void (*) (void *)) &nsync_mu_unlock,
						     abs_deadline, cancel_note));
}

/* Atomically release *pmu and block the caller on *pcv.  Wait
   until awakened by a call to nsync_cv_signal() or nsync_cv_broadcast(), or a spurious
   wakeup.  Then reacquires *pmu, and return.  The call is equivalent to a call
   to nsync_cv_wait_with_deadline() with abs_deadline==nsync_time_no_deadline, and a NULL
   cancel_note.  It should be used in a loop, as with all standard Mesa-style
   condition variables.  See examples above.  */
void nsync_cv_wait (nsync_cv *pcv, nsync_mu *pmu) {
	nsync_cv_wait_with_deadline (pcv, pmu, nsync_time_no_deadline, NULL);
}

static nsync_time cv_ready_time (void *v UNUSED, struct nsync_waiter_s *nw) {
	return (nw == NULL || ATM_LOAD_ACQ (&nw->waiting) != 0? nsync_time_no_deadline : nsync_time_zero);
}

static nsync_time cv_enqueue (void *v, struct nsync_waiter_s *nw) {
	nsync_cv *pcv = (nsync_cv *) v;
	/* acquire spinlock */
	uint32_t old_word = nsync_spin_test_and_set_ (&pcv->word, CV_SPINLOCK, CV_SPINLOCK, 0);
	pcv->waiters = nsync_dll_make_last_in_list_ (pcv->waiters, (nsync_dll_element_ *)nw->q);
	ATM_STORE (&nw->waiting, 1);
	/* Release spinlock. */
	ATM_STORE_REL (&pcv->word, old_word | CV_NON_EMPTY); /* release store */
	return (nsync_time_no_deadline);
}

static nsync_time cv_dequeue (void *v, struct nsync_waiter_s *nw) {
	nsync_cv *pcv = (nsync_cv *) v;
	nsync_time woken;
	/* acquire spinlock */
	uint32_t old_word = nsync_spin_test_and_set_ (&pcv->word, CV_SPINLOCK, CV_SPINLOCK, 0);
	woken = nsync_time_zero;
	if (ATM_LOAD_ACQ (&nw->waiting) != 0) {
		pcv->waiters = nsync_dll_remove_ (pcv->waiters, (nsync_dll_element_ *)nw->q);
		ATM_STORE (&nw->waiting, 0);
		woken = nsync_time_no_deadline;
	}
	if (nsync_dll_is_empty_ (pcv->waiters)) {
		old_word &= ~(CV_NON_EMPTY);
	}
	/* Release spinlock. */
	ATM_STORE_REL (&pcv->word, old_word); /* release store */
	return (woken);
}

const struct nsync_waitable_funcs_s nsync_cv_waitable_funcs = {
	&cv_ready_time,
	&cv_enqueue,
	&cv_dequeue
};

NSYNC_CPP_END_
