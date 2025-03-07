#ifndef _BASIC_OPERATIONS_PRIVATE_H_
#define _BASIC_OPERATIONS_PRIVATE_H_
#include <stddef.h> // size_t
#include "auxiliary_functions.private.h"

int	clean(void* ptr);
void	*alloc(size_t);
struct meta_information	*get_last_chunck_raw();
struct meta_information	*get_free_chunck(size_t size);
int merge_if_free(struct meta_information * meta_information_element, void *arg2);
int  memory_division(struct meta_information *meta_information_struct, size_t size);

#endif
