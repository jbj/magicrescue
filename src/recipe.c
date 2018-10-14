/*
 * Magic Rescue recipe code
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
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "array.h"
#include "recipe.h"


int op_string(const char *s, union param *p)
{
    return memcmp(s, p->string.s, p->string.l) == 0;
}

int op_int32(const char *s_signed, union param *p)
{
    const unsigned char *s = (unsigned char *)s_signed;
    return ((s[0]<<24 | s[1]<<16 | s[2]<<8 | s[3]) & p->int32.mask)
	== p->int32.val;
}

void op_destroy(struct operation *op)
{
    /* Primitive multi-dispatch.
     * Add a case here for every operation type that requires destruction */
    if (op->func == op_string) {
	string_destroy(&op->param.string);
    }
}

void recipe_init(struct recipe *r)
{
    array_init(&r->ops, sizeof(struct operation));
    r->extension[0]      = '\0';
    r->command           = NULL;
    r->postextract       = NULL;
    r->rename            = NULL;
    r->min_output_file   = 100;
    r->name              = NULL;
    r->allow_overlap     = 0;
}

void recipe_destroy(struct recipe *r)
{
    struct operation *op;

    array_foreach(&r->ops, op) {
	op_destroy(op);
    }
    array_destroy(&r->ops);
    free(r->command);
    free(r->postextract);
    free(r->rename);
    free(r->name);
}

/* vim: ts=8 sw=4 noet tw=80
 */
