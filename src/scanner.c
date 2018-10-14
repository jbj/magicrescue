/*
 * Magic Rescue scanner code
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

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "array.h"
#include "recipe.h"
#include "scanner.h"

const unsigned char *scanner_char(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset)
{
    return memchr(scanbuf, param->c, scanbuf_len);
}

const unsigned char *scanner_block(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset)
{
    long extrabytes = (long)offset & (param->block-1);
    if (extrabytes) {
	if ((size_t)(param->block - extrabytes) < scanbuf_len)
	    return scanbuf + param->block - extrabytes;
	else
	    return NULL;
    }
    return scanbuf;
}

const unsigned char *scanner_string(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset)
{
    const unsigned char *p = scanbuf + param->scanstring.magicoff;
    struct string string = param->scanstring.string;
    unsigned char magicchar = param->scanstring.magicchar;
    scanbuf_len += param->scanstring.magicoff;

    while ((size_t)(p - scanbuf) < scanbuf_len &&
	    (p = memchr(p, magicchar, scanbuf_len - (p-scanbuf)))) {
	p -= param->scanstring.magicoff;
	if (memcmp(p, string.s, string.l) == 0) {
	    return p;
	}
	p += param->scanstring.magicoff + 1;
    }
    return NULL;
}


void scanner_string_init(union scan_param *param)
{
    unsigned char winner = '\0', c;
    size_t i, winner_off = 0;

    /* This table represents the number of million occurrences of each character
     * on my 9GB sample partition. We want to select the character that has the
     * smallest score. */
    static const int scoretable[] = {
1747,126,57,39,66,30,26,25,63,45,91,20,35,22,22,36,33,20,21,17,25,17,16,15,23,
18,16,15,21,15,15,15,318,18,40,27,86,21,23,22,36,33,28,21,38,40,50,50,94,63,45,
39,43,35,34,31,44,34,30,29,43,35,41,18,29,69,32,40,59,63,39,32,27,40,20,23,41,
34,38,32,44,24,40,47,47,31,26,23,24,20,25,20,25,21,16,79,18,123,45,83,79,182,
130,49,54,115,24,33,86,61,107,106,63,20,111,103,151,64,36,32,37,35,20,18,21,22,
15,15,29,20,15,40,21,28,15,14,17,74,14,67,16,33,14,13,28,15,14,15,16,15,13,12,
14,13,14,13,15,13,14,13,17,14,13,15,16,13,13,14,15,13,14,13,14,13,13,13,15,13,
12,13,16,14,18,14,18,14,14,13,15,15,14,14,28,19,16,21,22,13,17,23,15,16,13,13,
14,13,14,13,19,14,16,14,14,13,13,13,17,16,13,14,14,16,14,14,18,14,14,14,15,16,
13,14,39,21,13,17,20,14,14,15,18,13,13,14,16,14,17,16,19,15,16,17,21,20,24,179};

    for (i = 0; i < param->scanstring.string.l; i++) {
	c = param->scanstring.string.s[i];
	
	if (scoretable[c] < scoretable[winner]) {
	    winner = c;
	    winner_off = i;
	}
    }

    param->scanstring.magicchar = winner;
    param->scanstring.magicoff  = winner_off;
}



struct scanner *scanner_new(void)
{
    struct scanner *scanner = calloc(1, sizeof(struct scanner));
    array_init(&scanner->recipes, sizeof(struct recipe));
    scanner->func = NULL;
    return scanner;
}

int scanner_compare(const struct scanner *a, const struct scanner *b)
{
    if (a->func == b->func && a->offset == b->offset) {

	if (a->func == scanner_string
		&& a->param.scanstring.string.l
		== b->param.scanstring.string.l) {
	    return memcmp(
		    a->param.scanstring.string.s,
		    b->param.scanstring.string.s,
		    a->param.scanstring.string.l);
	}

	return memcmp(&a->param, &b->param, sizeof(a->param));
    }
    return -1;
}

void scanner_destroy(struct scanner *scanner)
{
    struct recipe *recipe;
    array_foreach(&scanner->recipes, recipe) {
	recipe_destroy(recipe);
    }
    array_destroy(&scanner->recipes);
    
    /* Primitive multi-dispatch.
     * Add a case here for every operation type that requires destruction */
    if (scanner->func == scanner_string) {
	string_destroy(&scanner->param.scanstring.string);
    }
}


/* vim: ts=8 sw=4 noet tw=80
 */
