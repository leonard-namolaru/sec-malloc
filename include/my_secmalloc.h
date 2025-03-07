#ifndef _SECMALLOC_H /* garde d'inclusion pour éviter l'inclusion multiple */
#define _SECMALLOC_H

#include <stddef.h> // size_t

void    free(void *ptr);
void    *malloc(size_t size);
void    *calloc(size_t nmemb, size_t size);
void    *realloc(void *ptr, size_t size);

#endif
