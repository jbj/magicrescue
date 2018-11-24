#ifndef MAGICRESCUE_RECIPE_H
#define MAGICRESCUE_RECIPE_H

#include "array.h"

union param {
    struct string string;
    struct {
	unsigned long val;
	unsigned long mask;
    } int32;
};

typedef int (*op_function)(const char *, union param *);

struct operation {
    op_function func;
    union param param;
    int offset;
};

struct recipe {
    struct array ops;
    char extension[64];
    char *command;
    char *postextract;
    char *rename;
    long min_output_file;
    char *name;
    int allow_overlap;
};

int op_string(const char *s, union param *p);
int op_int32(const char *s, union param *p);

void op_destroy(struct operation *op);
void recipe_init(struct recipe *r);
void recipe_destroy(struct recipe *r);


#endif /* MAGICRESCUE_RECIPE_H */
