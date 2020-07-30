/* Pi-hole: A black hole for Internet advertisements
*  (c) 2017 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  Logging prototypes
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */
#ifndef LOG_H
#define LOG_H

#ifdef __FreeBSD__
#include <sys/cdefs.h>
#else
#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
	__attribute__ ((format (gnu_printf, fmtarg, firstvararg)))
#endif
#ifndef __malloc_like
#define __malloc_like __attribute__((__malloc__))
#endif
#endif

#include <stdbool.h>
#include <time.h>

void open_FTL_log(const bool test);
void logg(const char* format, ...) __printflike(1, 2);
void log_counter_info(void);
void format_memory_size(char *prefix, unsigned long long int bytes, double *formated);
const char *get_FTL_version(void) __malloc_like;
void log_FTL_version(bool crashreport);
void get_timestr(char *timestring, const time_t timein);

#endif //LOG_H
