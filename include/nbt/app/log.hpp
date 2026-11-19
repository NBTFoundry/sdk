// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_LOG_HPP
#define NBT_APP_LOG_HPP

#include <stdarg.h>

#if defined(__GNUC__)
#define NBT_APP_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define NBT_APP_PRINTF(fmt, args)
#endif

namespace nbt {
namespace app {

/* Messages go to stderr, one line each, prefixed "[tag] " (debug lines
 * "[tag][dbg] "). NBT Manager keeps an application's stdout and stderr as that
 * application's log, so an application needs no log file of its own. */

/* Sets the tag (at most 23 characters; "app" until set). */
void log_init(const char* tag);
const char* log_tag();

void log_set_debug(bool enabled);
bool log_debug_enabled();

void log_info(const char* fmt, ...) NBT_APP_PRINTF(1, 2);
void log_debug(const char* fmt, ...) NBT_APP_PRINTF(1, 2);
void log_info_v(const char* fmt, va_list ap);
void log_debug_v(const char* fmt, va_list ap);

/* The current stage, logged when set and named in a crash line. */
void log_stage(const char* stage);
const char* log_current_stage();

/* On SIGSEGV, SIGABRT, SIGBUS, SIGFPE and SIGILL: one line
 * "[tag] FATAL sig=... code=... addr=... stage=..." (plus pc, lr and sp, and
 * the library each lies in, on QNX ARM), then the default action. */
void log_install_crash_handlers();

}  // namespace app
}  // namespace nbt

#endif /* NBT_APP_LOG_HPP */
