#ifndef _PRELUDE_H_
#define _PRELUDE_H_

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

#ifndef __alloc_size
#define __alloc_size(size) __attribute__((__alloc_size__(size)))
#define __alloc_size2(num, size) __attribute__((__alloc_size__(num, size)))
#endif

#ifndef __pure2
#define __pure2 __attribute__((__const__))
#endif

#endif /* __FreeBSD__ */

#endif /* _PRELUDE_H_ */
