#pragma once

#include <stdlib.h>

#ifndef offsetof
#define offsetof(type, member) \
    ((size_t)&(((type *)0)->member))
#endif
