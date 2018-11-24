#ifndef MAGICRESCUE_ARRAY_H
#define MAGICRESCUE_ARRAY_H

#include <stdlib.h>

struct array {
    size_t el_len;
    size_t elements;
    void *data;
};

void array_init(struct array *array, size_t el_len);
void array_destroy(struct array *array);
void *array_el(struct array *array, size_t index);
size_t array_count(struct array *array);
void *array_add(struct array *array, void *el);

#define array_foreach(array, el) \
    for ((el)=(array)->data; \
	    (char *)(el) < (char *)(array)->data + \
		(array)->elements*(array)->el_len; \
	    (el) = (void *)((char *)(el) + (array)->el_len))

#endif /* MAGICRESCUE_ARRAY_H */
