/* Pi-hole: A black hole for Internet advertisements
*  (c) 2019 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  Daemon prototypes
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */
#ifndef DAEMON_H
#define DAEMON_H

#include "prelude.h"

void go_daemon(void);
void savepid(void);
char * getUserName(void);
void removepid(void);
void delay_startup(void);
bool is_fork(const pid_t mpid, const pid_t pid) __pure2;

#ifdef __FreeBSD__

#include <sys/thr.h>
static inline pid_t
FTL_gettid(void)
{
	long tid = -1;

	if (thr_self(&tid) == -1)
		return -1;

	return (pid_t)tid;
}

#else

#include <sys/syscall.h>
#include <unistd.h>
// Get ID of current thread (incorrectly shown as "PID" in, e.g., htop)
// We define this wrapper ourselves as the GNU C Library only added it
// in 2019 meaning that, while we're writing this, it will not be widely
// available. It was only added even later (end of 2019) to musl libc.
// https://sourceware.org/git/gitweb.cgi?p=glibc.git;h=1d0fc213824eaa2a8f8c4385daaa698ee8fb7c92
// https://www.openwall.com/lists/musl/2019/08/01/11
// To avoid any conflicts, also in the future, we use our own macro for this
#if !defined(SYS_gettid) && defined(__NR_gettid)
#define SYS_gettid __NR_gettid
#endif // !SYS_gettid && __NR_gettid
static inline pid_t
FTL_gettid(void)
{
#if defined(SYS_gettid)
	return (pid_t)syscall(SYS_gettid);
#else
#warning SYS_gettid or thr_self is not available on this system
	return -1;
#endif // SYS_gettid
}

#endif /* __FreeBSD__ */

#define gettid FTL_gettid

extern bool resolver_ready;

#endif //DAEMON_H
