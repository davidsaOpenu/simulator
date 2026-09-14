#pragma once

#include <stdio.h>

#define TRACE(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
