#ifndef EVSSIM_OPEN_OSD_COMPAT_LINUX_KERNEL_H
#define EVSSIM_OPEN_OSD_COMPAT_LINUX_KERNEL_H

#include <open-osd/linux/types.h>

#ifndef BUILD_BUG_ON
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2 * !!(condition)]))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))
#endif

#endif