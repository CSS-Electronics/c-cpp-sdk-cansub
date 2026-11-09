/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

/* Third-party includes. */
#include <mbedtls/threading.h>

/* Project includes. */
#include "threading_alt.h"

/***********************************************************************************************************************
 * External declarations.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public variable definition.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private definitions.
 **********************************************************************************************************************/

/* The opaque types of threading_alt.h are used as SRWLOCK and CONDITION_VARIABLE. The objects are only ever accessed
 * through the Windows API, which also initializes them. */
_Static_assert(sizeof(mbedtls_platform_mutex_t) == sizeof(SRWLOCK), "Mutex does not match SRWLOCK");
_Static_assert(_Alignof(mbedtls_platform_mutex_t) >= _Alignof(SRWLOCK), "Mutex alignment does not match SRWLOCK");
_Static_assert(sizeof(mbedtls_platform_condition_variable_t) == sizeof(CONDITION_VARIABLE), "Condition variable does not match CONDITION_VARIABLE");
_Static_assert(_Alignof(mbedtls_platform_condition_variable_t) >= _Alignof(CONDITION_VARIABLE), "Condition variable alignment does not match CONDITION_VARIABLE");

/***********************************************************************************************************************
 * Private type definitions.
 **********************************************************************************************************************/

/***********************************************************************************************************************
* Private variable definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function declarations.
 **********************************************************************************************************************/

static PSRWLOCK cansub_threading_srwlock(mbedtls_platform_mutex_t *mutex);
static PCONDITION_VARIABLE cansub_threading_condition(mbedtls_platform_condition_variable_t *cond);

static int cansub_threading_mutex_init(mbedtls_platform_mutex_t *mutex);
static void cansub_threading_mutex_destroy(mbedtls_platform_mutex_t *mutex);
static int cansub_threading_mutex_lock(mbedtls_platform_mutex_t *mutex);
static int cansub_threading_mutex_unlock(mbedtls_platform_mutex_t *mutex);

static int cansub_threading_cond_init(mbedtls_platform_condition_variable_t *cond);
static void cansub_threading_cond_destroy(mbedtls_platform_condition_variable_t *cond);
static int cansub_threading_cond_signal(mbedtls_platform_condition_variable_t *cond);
static int cansub_threading_cond_broadcast(mbedtls_platform_condition_variable_t *cond);
static int cansub_threading_cond_wait(mbedtls_platform_condition_variable_t *cond, mbedtls_platform_mutex_t *mutex);

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/

void cansub_threading_alt_set(void)
{
    mbedtls_threading_set_alt(cansub_threading_mutex_init,
                              cansub_threading_mutex_destroy,
                              cansub_threading_mutex_lock,
                              cansub_threading_mutex_unlock,
                              cansub_threading_cond_init,
                              cansub_threading_cond_destroy,
                              cansub_threading_cond_signal,
                              cansub_threading_cond_broadcast,
                              cansub_threading_cond_wait);
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/

static PSRWLOCK cansub_threading_srwlock(mbedtls_platform_mutex_t *mutex)
{
    return (PSRWLOCK)(void *)mutex;
}

static PCONDITION_VARIABLE cansub_threading_condition(mbedtls_platform_condition_variable_t *cond)
{
    return (PCONDITION_VARIABLE)(void *)cond;
}

static int cansub_threading_mutex_init(mbedtls_platform_mutex_t *mutex)
{
    InitializeSRWLock(cansub_threading_srwlock(mutex));

    return 0;
}

static void cansub_threading_mutex_destroy(mbedtls_platform_mutex_t *mutex)
{
    /* An SRW lock holds no resources. */
    (void)mutex;
}

static int cansub_threading_mutex_lock(mbedtls_platform_mutex_t *mutex)
{
    /* SRW locks are not recursive, which mbedTLS does not need: it never locks a mutex it already holds. */
    AcquireSRWLockExclusive(cansub_threading_srwlock(mutex));

    return 0;
}

static int cansub_threading_mutex_unlock(mbedtls_platform_mutex_t *mutex)
{
    ReleaseSRWLockExclusive(cansub_threading_srwlock(mutex));

    return 0;
}

static int cansub_threading_cond_init(mbedtls_platform_condition_variable_t *cond)
{
    InitializeConditionVariable(cansub_threading_condition(cond));

    return 0;
}

static void cansub_threading_cond_destroy(mbedtls_platform_condition_variable_t *cond)
{
    /* A condition variable holds no resources. */
    (void)cond;
}

static int cansub_threading_cond_signal(mbedtls_platform_condition_variable_t *cond)
{
    WakeConditionVariable(cansub_threading_condition(cond));

    return 0;
}

static int cansub_threading_cond_broadcast(mbedtls_platform_condition_variable_t *cond)
{
    WakeAllConditionVariable(cansub_threading_condition(cond));

    return 0;
}

static int cansub_threading_cond_wait(mbedtls_platform_condition_variable_t *cond, mbedtls_platform_mutex_t *mutex)
{
    int result = 0;

    /* Flags 0: the lock is held exclusively, as taken by cansub_threading_mutex_lock(). Spurious wakeups are allowed. */
    if (!SleepConditionVariableSRW(cansub_threading_condition(cond), cansub_threading_srwlock(mutex), INFINITE, 0))
    {
        result = MBEDTLS_ERR_THREADING_USAGE_ERROR;
    }

    return result;
}
