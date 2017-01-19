#ifndef NSYNC_PLATFORM_WIN32_PLATFORM_H_
#define NSYNC_PLATFORM_WIN32_PLATFORM_H_

#include <Windows.h>
#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>

#include "compiler.h"
#include "nsync_cpp.h"


NSYNC_C_START_
/* Can't use TlsAlloc() because we use pthread_key_create's destructor
   argument, so we implement it anew. */
#define pthread_key_t nsync_pthread_key_t
typedef int nsync_pthread_key_t;
int nsync_pthread_key_create (nsync_pthread_key_t *pkey, void (*dest) (void *));
int nsync_pthread_key_delete (nsync_pthread_key_t key);
void *nsync_pthread_getspecific (nsync_pthread_key_t key);
int nsync_pthread_setspecific (nsync_pthread_key_t key, void *value);
#define pthread_key_create nsync_pthread_key_create
#define pthread_key_delete nsync_pthread_key_delete
#define pthread_getspecific nsync_pthread_getspecific
#define pthread_setspecific nsync_pthread_setspecific
NSYNC_C_END_

NSYNC_CPP_START_

#define CLOCK_REALTIME 0
#define TIMER_ABSTIME 1
#define clockid_t nsync_clockid_t
typedef int nsync_clockid_t;
int nsync_clock_gettime (clockid_t clk_id, struct timespec *tp);
#define clock_gettime nsync_clock_gettime
int nsync_nanosleep(const struct timespec *delay, struct timespec *remaining);
#define nanosleep nsync_nanosleep

#define pthread_mutex_t CRITICAL_SECTION
#define pthread_mutex_lock EnterCriticalSection
#define pthread_mutex_unlock LeaveCriticalSection
#define pthread_mutex_init(mu,attr) InitializeCriticalSection (mu)
#define pthread_mutex_destroy DeleteCriticalSection

#define pthread_cond_t CONDITION_VARIABLE
#define pthread_cond_broadcast WakeAllConditionVariable
#define pthread_cond_signal WakeConditionVariable
#define pthread_cond_wait(cv, mu) SleepConditionVariableCS ((cv), (mu), INFINITE)
#define pthread_cond_timedwait nsync_pthread_cond_timedwait
#define pthread_cond_init(cv, attr) InitializeConditionVariable (cv)
#define pthread_cond_destroy(cv) /*no-op*/
int nsync_pthread_cond_timedwait (pthread_cond_t *cv, pthread_mutex_t *mu,
		                  const struct timespec *abstimeout);
typedef struct {
	SRWLOCK srw;
	int w;
} nsync_pthread_rwlock_t;
#define pthread_rwlock_t nsync_pthread_rwlock_t
#define pthread_rwlock_wrlock(rw) do { AcquireSRWLockExclusive (&(rw)->srw); (rw)->w = 1; } while (0)
#define pthread_rwlock_rdlock(rw) AcquireSRWLockShared (&(rw)->srw)
#define pthread_rwlock_unlock(rw) do { if ((rw)->w) { (rw)->w = 0; ReleaseSRWLockExclusive (&(rw)->srw); } else { ReleaseSRWLockShared (&(rw)->srw); } } while (0)
#define pthread_rwlock_init(rw,attr) do { InitializeSRWLock (&(rw)->srw); (rw)->w = 0; } while (0) 
#define pthread_rwlock_destroy(rw) /*no-op*/


#define pthread_once_t INIT_ONCE
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
extern BOOL CALLBACK nsync_init_callback_ (pthread_once_t *o, void *v, void **c);
#define pthread_once(o, f) do { \
				void (*ff) = (f); \
				InitOnceExecuteOnce ((o),  &nsync_init_callback_, &ff, NULL); \
			   } while (0)

#define sched_yield() Sleep(0)

NSYNC_CPP_END_

#endif /*NSYNC_PLATFORM_WIN32_PLATFORM_H_*/
