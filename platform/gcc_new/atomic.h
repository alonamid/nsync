#ifndef NSYNC_PLATFORM_GCC_NEW_ATOMIC_H_
#define NSYNC_PLATFORM_GCC_NEW_ATOMIC_H_

/* Atomic operations on nsync_atomic_uint32_ quantities
   CAS, load, and store.

   Normally, these are used only on nsync_atomic_uint32_ values, but on Linux they may be
   invoked on int values, because futexes operate on int values.  A
   compile-time check in the futex code ensures that both int and   
   nsync_atomic_uint32_ are 32 bits.

   Memory barriers:
	   Operations with the suffixes _ACQ and _RELACQ ensure that the operation
	   appears to complete before other memory operations subsequently performed by
	   the same thread, as seen by other threads.  (In the case of ATM_CAS_ACQ,
	   this applies only if the operation returns a non-zero value.)

	   Operations with the suffixes _REL and _RELACQ ensure that the operation
	   appears to complete after other memory operations previously performed by
	   the same thread, as seen by other threads.  (In the case of ATM_CAS_REL,
	   this applies only if the operation returns a non-zero value.)

   // Atomically,
   //   int ATM_CAS (nsync_atomic_uint32_ *p, uint32_t old_value, uint32_t new_value) {
   //		if (*p == old_value) {
   //			*p = new_value;
   //			return (some-non-zero-value);
   //		} else {
   //			return (0);
   //		}
   //	}
   // *_ACQ, *_REL, *_RELACQ variants are available,
   // with the barrier semantics described above.
   int ATM_CAS (nsync_atomic_uint32_ *p, uint32_t old_value, uint32_t new_value);

   // Atomically,
   //     uint32_t ATM_LOAD (nsync_atomic_uint32_ *p) { return (*p); }
   // A *_ACQ variant is available,
   // with the barrier semantics described above.
   uint32_t ATM_LOAD (nsync_atomic_uint32_ *p);

   // Atomically,
   //     void ATM_STORE (nsync_atomic_uint32_ *p, uint32_t value) { *p = value; }
   // A *_REL variant is available,
   // with the barrier semantics described above.
   void ATM_STORE (nsync_atomic_uint32_ *p, uint32_t value);
 */

#include "compiler.h" 
#include "nsync_atomic.h"

NSYNC_CPP_START_

/* GCC 4.7 or later --- has new-style atomic ops */

static __inline__ int atm_cas_nomb_u32_ (nsync_atomic_uint32_ *p, uint32_t o, uint32_t n) {
	return (__atomic_compare_exchange_n (NSYNC_ATOMIC_UINT32_PTR_ (p), &o, n, 0,
					     __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}
static __inline__ int atm_cas_acq_u32_ (nsync_atomic_uint32_ *p, uint32_t o, uint32_t n) {
	return (__atomic_compare_exchange_n (NSYNC_ATOMIC_UINT32_PTR_ (p), &o, n, 0,
					     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
}
static __inline__ int atm_cas_rel_u32_ (nsync_atomic_uint32_ *p, uint32_t o, uint32_t n) {
	return (__atomic_compare_exchange_n (NSYNC_ATOMIC_UINT32_PTR_ (p), &o, n, 0,
					     __ATOMIC_RELEASE, __ATOMIC_RELAXED));
}
static __inline__ int atm_cas_relacq_u32_ (nsync_atomic_uint32_ *p, uint32_t o, uint32_t n) {
	return (__atomic_compare_exchange_n (NSYNC_ATOMIC_UINT32_PTR_ (p), &o, n, 0,
					     __ATOMIC_ACQ_REL, __ATOMIC_RELAXED));
}

#define ATM_CAS_HELPER_(barrier, p, o, n) (atm_cas_##barrier##_u32_ ((p), (o), (n)))

#define ATM_CAS(p,o,n)           ATM_CAS_HELPER_ (nomb,   (p), (o), (n))
#define ATM_CAS_ACQ(p,o,n)       ATM_CAS_HELPER_ (acq,    (p), (o), (n))
#define ATM_CAS_REL(p,o,n)       ATM_CAS_HELPER_ (rel,    (p), (o), (n))
#define ATM_CAS_RELACQ(p,o,n)    ATM_CAS_HELPER_ (relacq, (p), (o), (n))

#define ATM_LOAD(p)         (__atomic_load_n (NSYNC_ATOMIC_UINT32_PTR_ (p), __ATOMIC_RELAXED))
#define ATM_LOAD_ACQ(p)     (__atomic_load_n (NSYNC_ATOMIC_UINT32_PTR_ (p), __ATOMIC_ACQUIRE))

#define ATM_STORE(p,v)      (__atomic_store_n (NSYNC_ATOMIC_UINT32_PTR_ (p), (v), __ATOMIC_RELAXED))
#define ATM_STORE_REL(p,v)  (__atomic_store_n (NSYNC_ATOMIC_UINT32_PTR_ (p), (v), __ATOMIC_RELEASE))

NSYNC_CPP_END_

#endif /*NSYNC_PLATFORM_GCC_NEW_ATOMIC_H_*/
