#ifndef MAGICRESCUE_SCANNER_H
#define MAGICRESCUE_SCANNER_H

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

typedef const char *(*scan_function)(const char *, size_t,
	union scan_param *, off_t);

struct scanner {
    scan_function func;
    union scan_param param;
    int offset;
    int extra_len;
    struct array recipes;
};

const char *scanner_char(const char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset);
const char *scanner_block(const char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset);
const char *scanner_string(const char *scanbuf,
	size_t scanbuf_len, union scan_param *param, off_t offset);

void scanner_string_init(union scan_param *param);

struct scanner *scanner_new(void);
int scanner_compare(const struct scanner *a, const struct scanner *b);
void scanner_destroy(struct scanner *scanner);

#endif /* MAGICRESCUE_SCANNER_H */
