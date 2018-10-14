/*
 * Magic Rescue misc helper code
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

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"


/** Returns the value (0-15) of a single hex digit. Returns 0 on error. */
int hex2num(unsigned char c)
{
    return  (c >= '0' && c <= '9' ? c-'0' :
	    (c >= 'a' && c <= 'f' ? c-'a'+10 :
	    (c >= 'A' && c <= 'F' ? c-'A'+10 : 0)));
}

long long hextoll(const unsigned char *str)
{
    long long result = 0;
    size_t i, len = strlen(str);

    for (i = 0; i < len; i++) {
	result |= (long long)hex2num(str[i]) << 4*((len-1) - i);
    }
    return result;
}

off_t rich_seek(int fd, const char *string)
{
    off_t offset;
    int whence = SEEK_SET;

    if (string) {
	const char *stroffset = string;
	int sign = 1;

	if (strchr("=+-", stroffset[0])) {
	    switch (stroffset[0]) {
		case '+': whence = SEEK_CUR; break;
		case '-': whence = SEEK_END; sign = -1; break;
	    }
	    stroffset++;
	}

	if (stroffset[0] == '0' && stroffset[1] == 'x') {
	    offset = (off_t)hextoll(stroffset + 2);
	} else {
#ifdef HAVE_ATOLL
	    offset = (off_t)atoll(stroffset);
#else
	    errno = ENOSYS;
	    fprintf(stderr, "Get a C99 compiler or specify offset in hex\n");
	    return -1;
#endif /* not HAVE_ATOLL */
	}

	offset *= sign;

    } else {
	offset = 0;
	whence = SEEK_CUR;
    }

    return lseek(fd, offset, whence);
}

/** Initializes a struct string from a 0-terminated string, parsing escape
 * sequences. The struct string will not be 0-terminated. */
void string_init(struct string *dst, const unsigned char *src)
{
    const size_t slen = strlen(src);
    size_t i;
    dst->l = 0;
    dst->s = malloc(slen);

    for (i = 0; i < slen; i++) {
	if (src[i] == '\\') {
	    if (src[i+1] == 'x' && i + 3 < slen) {
		dst->s[dst->l++] = (hex2num(src[i+2])<<4) | hex2num(src[i+3]);
		i += 3;

	    } else {
		dst->s[dst->l++] = (
			src[i+1] == 'a' ? '\a' : (
			src[i+1] == 'b' ? '\b' : (
			src[i+1] == 'f' ? '\f' : (
			src[i+1] == 'n' ? '\n' : (
			src[i+1] == 'r' ? '\r' : (
			src[i+1] == 't' ? '\t' : (
			src[i+1] == 'v' ? '\v' : (
			src[i+1] ))))))));
		i++;
	    }

	} else {
	    dst->s[dst->l++] = src[i];
	}
    }

    /* dst->s = realloc(dst->s, dst->l); // who cares? */
}

void string_destroy(struct string *string)
{
    free(string->s);
}

/* vim: ts=8 sw=4 noet tw=80
 */
