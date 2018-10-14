/*
 * Magic Rescue array routines
 * Copyright (C) 2004 Jonas Jensen <jbj@knef.dk>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <stdlib.h>
#include <string.h>

#include <array.h>

void array_init(struct array *array, size_t el_len)
{
    array->el_len = el_len;
    array->elements = 0;
    array->data = NULL;
}

void array_destroy(struct array *array)
{
    free(array->data);
}

void *array_el(struct array *array, size_t index)
{
    return (char *)array->data + index * array->el_len;
}

size_t array_count(struct array *array)
{
    return array->elements;
}

void *array_add(struct array *array, void *el)
{
    array->data = realloc(array->data, ++array->elements * array->el_len);
    if (el) {
	char *tmp = array->data;
	memcpy(&tmp[(array->elements-1) * array->el_len], el, array->el_len);
    }
    return array_el(array, array->elements-1);
}

/* vim: ts=8 sw=4 noet tw=80
 */
