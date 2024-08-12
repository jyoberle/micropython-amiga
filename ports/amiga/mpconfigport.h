/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Yuichi Nakamura
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Options controlling how MicroPython is built, overriding defaults in py/mpconfig.h

#include <stdint.h>
#include <limits.h>

typedef unsigned long suseconds_t;

// options to control how MicroPython is built

// Use the minimal starting configuration (disables all optional features).
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EVERYTHING)

// Board and hardware specific configuration
#define MICROPY_HW_MCU_NAME            "m68000"
#define MICROPY_HW_BOARD_NAME          "Amiga"
#define MICROPY_PY_SYS_PLATFORM        "amigaOS"
#define MICROPY_OPTION_AMIGA            (1)

// Enable helpers for printing debugging information.
#ifndef MICROPY_DEBUG_PRINTERS
#define MICROPY_DEBUG_PRINTERS         (1)
#endif

// Enable floating point by default.
#define MICROPY_FLOAT_IMPL             (MICROPY_FLOAT_IMPL_DOUBLE)

#define MICROPY_CPYTHON_COMPAT      (1)
#define MICROPY_USE_INTERNAL_PRINTF (1)

// Enable arbitrary precision long-int by default.
#define MICROPY_LONGINT_IMPL           (MICROPY_LONGINT_IMPL_MPZ)

// REPL conveniences.
#define MICROPY_REPL_EMACS_WORDS_MOVE  (1)
#define MICROPY_REPL_EMACS_EXTRA_WORDS_MOVE (1)
#define MICROPY_USE_READLINE_HISTORY   (1)
#ifndef MICROPY_READLINE_HISTORY_SIZE
#define MICROPY_READLINE_HISTORY_SIZE  (50)
#endif

// Allow exception details in low-memory conditions.
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE (0)

// Allow loading of .mpy files.
#define MICROPY_PERSISTENT_CODE_LOAD   (1)

// Enable a small performance boost for the VM.
#define MICROPY_OPT_COMPUTED_GOTO      (1)

// Enable detailed error messages.
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_DETAILED)

// Configure the "sys" module with features not usually enabled on bare-metal.
#define MICROPY_PY_SYS_ATEXIT          (1)

// Configure the "os" module with extra unix features.
#define MICROPY_PY_OS_INCLUDEFILE      "ports/amiga/modos.c"
#define MICROPY_PY_OS_ERRNO            (1)
#define MICROPY_PY_OS_GETENV_PUTENV_UNSETENV (1)
#define MICROPY_PY_OS_SEP              (1)
#define MICROPY_PY_OS_SYSTEM           (1)
#define MICROPY_PY_OS_UNAME            (1)
#define MICROPY_PY_OS_STATVFS          (1)
#define MICROPY_PY_OS_URANDOM          (0)
#define MICROPY_PY_OS_SYNC             (0)

// Enable the unix-specific "time" module.
#define MICROPY_PY_TIME                (1)
#define MICROPY_PY_TIME_TIME_TIME_NS   (1)
#define MICROPY_PY_TIME_INCLUDEFILE    "ports/amiga/modtime.c"

// Enable the "machine" module, mostly for machine.mem*.
#define MICROPY_PY_MACHINE             (1)
#define MICROPY_PY_MACHINE_PULSE       (1)
#define MICROPY_PY_MACHINE_PIN_BASE    (1)

#ifndef MICROPY_PY_SYS_PATH_DEFAULT
#define MICROPY_PY_SYS_PATH_DEFAULT ".frozen"
#endif

#define MP_STATE_PORT MP_STATE_VM

// Configure which emitter to use for this target.
#define MICROPY_EMIT_INLINE_M68K    (1)
#define MICROPY_EMIT_M68K           (1)

// control over Python builtins
#define MICROPY_PY_THREAD                   (0)

// Type definitions for the specific machine based on the word size.
typedef intptr_t mp_int_t; // must be pointer size
typedef uintptr_t mp_uint_t; // must be pointer size

// Cannot include <sys/types.h>, as it may lead to symbol name clashes
typedef long mp_off_t;

#define MICROPY_OBJ_BASE_ALIGNMENT  __attribute__((aligned(4)))

// We need to provide a declaration/definition of alloca()
#include <alloca.h>

// Always enable GC.
#define MICROPY_ENABLE_GC           (1)

#if !(defined(MICROPY_GCREGS_SETJMP))
// Fall back to setjmp() implementation for discovery of GC pointers in registers.
#define MICROPY_GCREGS_SETJMP (1)
#endif

// Enable the VFS
#define MICROPY_ENABLE_FINALISER    (1)
#define MICROPY_VFS                 (1)
#define MICROPY_READER_VFS          (0)
#define MICROPY_HELPER_LEXER_UNIX   (1)
#define MICROPY_VFS_POSIX           (0)
#define MICROPY_READER_POSIX        (1)

#define MICROPY_PY_SOCKET (1)
#define MICROPY_PY_NETWORK (0)

// VFS stat functions should return time values relative to 1970/1/1
#define MICROPY_EPOCH_IS_1970       (1)

// Disable stackless by default.
#ifndef MICROPY_STACKLESS
#define MICROPY_STACKLESS           (0)
#define MICROPY_STACKLESS_STRICT    (0)
#endif

// Implementation of the machine module.
#define MICROPY_PY_MACHINE_INCLUDEFILE "ports/amiga/modmachine.c"

#define MICROPY_ALLOC_PATH_MAX      (PATH_MAX)

// Ensure builtinimport.c works with -m.
#define MICROPY_MODULE_OVERRIDE_MAIN_IMPORT (1)

// Don't default sys.argv and sys.path because we do that in main.
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS (0)

// Enable sys.executable.
#define MICROPY_PY_SYS_EXECUTABLE (1)

// Bare-metal ports don't have stderr. Printing debug to stderr may give tests
// which check stdout a chance to pass, etc.
extern const struct _mp_print_t mp_stderr_print;
#define MICROPY_DEBUG_PRINTER (&mp_stderr_print)
#define MICROPY_ERROR_PRINTER (&mp_stderr_print)

// If enabled, configure how to seed random on init.
#ifdef MICROPY_PY_RANDOM_SEED_INIT_FUNC
#include <stddef.h>
void mp_hal_get_random(size_t n, void *buf);
static inline unsigned long mp_random_seed_init(void) {
    unsigned long r;
    mp_hal_get_random(sizeof(r), &r);
    return r;
}
#endif

#define MICROPY_USE_READLINE (1)

#define MICROPY_DEBUG_PARSE_RULE_NAME (1)

#ifndef __APPLE__
// For debugging purposes, make printf() available to any source file.
#include <stdio.h>
#endif

// In lieu of a WFI(), slow down polling from being a tight loop.
//
// Note that we don't delay for the full TIMEOUT_MS, as execution
// can't be woken from the delay.
#define MICROPY_INTERNAL_WFE(TIMEOUT_MS) mp_hal_delay_us(500)

#ifndef MICROPY_EVENT_POLL_HOOK
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
    } while (0);
#endif

// Python internal features.
#define MICROPY_SMALL_INT_MUL_HELPER            (0)

#define MICROPY_SCHEDULER_STATIC_NODES          (1)

#define MICROPY_PY_SELECT                       (1)
#define MICROPY_PY_ASYNC_AWAIT                  (1)
#define MICROPY_PY_ASYNCIO                      (1)
#define MICROPY_STREAMS_NON_BLOCK               (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)

#define MICROPY_PY_BUILTINS_STR_UNICODE         (1)
#define MICROPY_PY_BUILTINS_STR_SJIS            (1)
#define MICROPY_PY_BUILTINS_STR_SJIS_CHECK      (1)

#define MICROPY_PY_RE                           (1)
#define MICROPY_PY_RE_MATCH_GROUPS              (1)
#define MICROPY_PY_RE_MATCH_SPAN_START_END      (1)
#define MICROPY_PY_RE_SUB                       (1)

#define MP_SSIZE_MAX (0x7fffffff)

// Seed random on import.
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (mp_random_seed_init())

// If enabled, configure how to seed random on init.
#ifdef MICROPY_PY_RANDOM_SEED_INIT_FUNC
#include <stddef.h>
void mp_hal_get_random(size_t n, void *buf);
static inline unsigned long mp_random_seed_init(void) {
    unsigned long r;
    mp_hal_get_random(sizeof(r), &r);
    return r;
}
#endif

#define MICROPY_MIN_USE_STDOUT (1)
#define MICROPY_HEAP_SIZE      (25600) // heap size 25 kilobytes

