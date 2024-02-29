#define HAVE_UNISTD_H
#define HAVE_STDLIB_H
#define HAVE_SYS_TIME_H
#define HAVE_SYS_FILE_H
#define HAVE_SYS_IOCTL_H
#define HAVE_SYS_SOCKET_H
#define HAVE_SYS_UN_H

#define HAVE_NANOSLEEP
#define HAVE_USLEEP

// copied from c.h
#include <fcntl.h>

#ifdef O_CLOEXEC
#define UL_CLOEXECSTR	"e"
#else
#define UL_CLOEXECSTR	""
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
