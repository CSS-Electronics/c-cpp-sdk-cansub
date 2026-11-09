/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_LOCK_H
#define CANSUB_LOCK_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#else
#include <pthread.h>
#endif

/* Project includes. */

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

#if defined(_WIN32)

typedef SRWLOCK cansub_lock;
#define CANSUB_LOCK_INIT SRWLOCK_INIT

static inline void cansub_lock_acquire(cansub_lock *lock)
{
    AcquireSRWLockExclusive(lock);
}

static inline void cansub_lock_release(cansub_lock *lock)
{
    ReleaseSRWLockExclusive(lock);
}

#else

typedef pthread_mutex_t cansub_lock;
#define CANSUB_LOCK_INIT PTHREAD_MUTEX_INITIALIZER

static inline void cansub_lock_acquire(cansub_lock *lock)
{
    pthread_mutex_lock(lock);
}

static inline void cansub_lock_release(cansub_lock *lock)
{
    pthread_mutex_unlock(lock);
}

#endif

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

#endif