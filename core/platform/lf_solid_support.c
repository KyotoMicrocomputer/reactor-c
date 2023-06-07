#if defined(PLATFORM_SOLID)
/* SOLID support for the C target of Lingua Franca. */

/*************
Copyright (c) 2022, The University of California at Berkeley.
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************/

/** SOLID support for the C target of Lingua Franca.
 *
 *  @author{Daisuke Sato <sato@kmckk.co.jp>}
 */


#include "lf_solid_support.h"
#include "../platform.h"
#include <stdarg.h>
#include <solid_mutex.h>
#include <solid_timer.h>
#include <solid_cs_assert.h>
#include <solid_log.h>
#include <kernel.h>
#ifdef USE_NETLOGGER
#include <netlogger.h>
#endif
#ifndef LF_THREADED
// Keep track of physical actions being entered into the system
static volatile bool _lf_async_event = false;
#endif

static volatile double _nw_nsecPerTick;

static void wait_nsec(uint32_t nsec) {
    uint64_t now = SOLID_TIMER_GetCurrentTick();
    const uint64_t until = now + (uint64_t)((double)nsec / _nw_nsecPerTick);

    if (now >= until)
        return;

    for (;;) {
        now = SOLID_TIMER_GetCurrentTick();
        if (now >= until)
            break;
    }
}

/**
 * @brief Sleep until an absolute time.
 * TODO: For improved power consumption this should be implemented with a HW timer and interrupts.
 *
 * @param wakeup int64_t time of wakeup
 * @return int 0 if successful sleep, -1 if awoken by async event
 */
int lf_sleep_until_locked(instant_t wakeup) {
#ifndef LF_THREADED
    _lf_async_event = false;
    lf_critical_section_exit();
#endif
    instant_t now;
    lf_clock_gettime(&now);
    interval_t sleep_duration = wakeup - now;
    if (sleep_duration < 0)
        return 0;
    wait_nsec(sleep_duration);
#ifndef LF_THREADED
    lf_critical_section_enter();

    if (_lf_async_event) {
        _lf_async_event = false;
        return -1;
    }
#endif
    return 0;
}

/**
 * @brief Sleep for a specified duration.
 *
 * @param sleep_duration int64_t nanoseconds representing the desired sleep duration
 * @return int 0 if success. -1 if interrupted by async event.
 */
int lf_sleep(interval_t sleep_duration) {
    if (sleep_duration < 0)
        return 0;
    if (sleep_duration < 1000) {
        wait_nsec(sleep_duration);
        return 0;
    }
    interval_t sleep_usec = (sleep_duration + 500) / 1000;
    dly_tsk(sleep_usec);
    return 0;
}

void lf_initialize_clock() {
    _nw_nsecPerTick = (double)(1000000000.0 / (double)SOLID_TIMER_GetTicksPerSec());
}

int lf_clock_gettime(instant_t* t) {
    solid_cs_assert(t != NULL);

    *t = (instant_t)((double)SOLID_TIMER_GetCurrentTick() * _nw_nsecPerTick);

    return 0;
}

#ifndef LF_THREADED

static SOLID_CRITICALSECTION_T _critical_section;
static bool _critical_section_initialized = false;

int lf_critical_section_enter() {
    if (_critical_section_initialized == false) { 
        SOLID_InitializeCriticalSection(&_critical_section);
        _critical_section_initialized = true;
    }
    SOLID_EnterCriticalSection(&_critical_section);
    return 0;
}

/**
 * @brief Exit a critical section.
 */
int lf_critical_section_exit() {
    SOLID_LeaveCriticalSection(&_critical_section);
    return 0;
}

/**
 * Handle notifications from the runtime of changes to the event queue.
 * If a sleep is in progress, it should be interrupted.
*/
int lf_notify_of_event() {
	_lf_async_event = true;
   return 0;
}

#else
#warning "Threaded support on SOLID is still experimental"

// Typedef that represents the function pointers passed by LF runtime into lf_thread_create
typedef void *(*lf_function_t) (void *);

/**
 * @brief Get the number of cores on the host machine.
 */
int lf_available_cores() {
    return TNUM_PRCID;
}
#endif

void *calloc(size_t n, size_t size) {
    void *mem = malloc(n * size);
    if (mem)
        memset(mem, 0, n * size);
    return mem;
}

int printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, 1024, fmt, ap);
    va_end(ap);

    buf[ret++] = '\0';

#ifdef USE_NETLOGGER
    if (nl_is_initialized()) {
        nl_printf(buf);
        return ret;
    }
#endif
    SOLID_LOG_write(buf, ret);
    return ret;
}

void lf_print(const char *format, ...) {
    char buf[1024];
    va_list ap;

    va_start(ap, format);
    int ret = vsnprintf(buf, 1024, format, ap);
    va_end(ap);

    buf[ret++] = '\n';
    buf[ret++] = '\0';

#ifdef USE_NETLOGGER
    if (nl_is_initialized()) {
        nl_printf(buf);
        return;
    }
#endif
    SOLID_LOG_write(buf, ret);
}

void lf_print_debug(const char* format, ...) {
    char buf[1024];
    va_list ap;

    va_start(ap, format);
    int ret = vsnprintf(buf, 1024, format, ap);
    va_end(ap);

    buf[ret++] = '\n';
    buf[ret++] = '\0';

#ifdef USE_NETLOGGER
    if (nl_is_initialized()) {
        nl_printf("[DEBUG] ");
        nl_printf(buf);
        return;
    }
#endif
    SOLID_LOG_write("[DEBUG] ", 8);
    SOLID_LOG_write(buf, ret);
}

void lf_print_log(const char *format, ...) {
    char buf[1024];
    va_list ap;

    va_start(ap, format);
    int ret = vsnprintf(buf, 1024, format, ap);
    va_end(ap);

    buf[ret++] = '\n';
    buf[ret++] = '\0';

#ifdef USE_NETLOGGER
    if (nl_is_initialized()) {
        nl_printf("[LOG] ");
        nl_printf(buf);
        return;
    }
#endif
    SOLID_LOG_write("[LOG] ", 6);
    SOLID_LOG_write(buf, ret);
}

#endif
