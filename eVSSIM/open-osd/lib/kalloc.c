#include "kalloc.h"
#include <stdlib.h>
#include <unistd.h>

#define __unused			__attribute__((unused))

void *kalloc(size_t size, gfp_t unused __unused)
{
	return malloc(size);
}

void *kzalloc(size_t size, gfp_t unused __unused)
{
	return calloc(size, 1);
}

void *krealloc(const void *p, size_t new_size, gfp_t flags __unused)
{
	return realloc((void *)p, new_size);
}

void kfree(const void *objp)
{
	if (!objp) /* is free NULL safe? */
		return;

	free((void *)objp);
}

unsigned long __get_free_page(gfp_t unused __unused)
{
	void *ptr;
	size_t page_size = getpagesize();

	if (posix_memalign(&ptr, page_size, page_size))
		return 0;

	return (unsigned long)ptr;
}

void free_page(unsigned long addr)
{
	free((void *)addr);
}
