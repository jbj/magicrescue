#ifndef _SCANNER_H

#include <stdlib.h>

#include "array.h"
#include "recipe.h"

union scan_param {
    char c;
    long block;
    struct {
	struct string string;
	char magicchar;
	int magicoff;
    } scanstring;
};

typedef const unsigned char *(*scan_function)(const unsigned char *, size_t,
	union scan_param *, off_t);

struct scanner {
    scan_function func;
    union scan_param param;
    int offset;
    int extra_len;
    struct array recipes;
};

const unsigned char *scanner_char(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset);
const unsigned char *scanner_block(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset);
const unsigned char *scanner_string(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset);

void scanner_string_init(union scan_param *param);

struct scanner *scanner_new(void);
int scanner_compare(const struct scanner *a, const struct scanner *b);
void scanner_destroy(struct scanner *scanner);

#define _SCANNER_H
#endif /* _SCANNER_H */
