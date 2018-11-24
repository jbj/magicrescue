#ifndef MAGICRESCUE_RECUR_H
#define MAGICRESCUE_RECUR_H

#include <sys/stat.h>

struct dirstack;
struct recur;

void rm_rf(const char *dir);
struct dirstack *dirstack_open(const char *path);
void dirstack_close(struct dirstack *stack);
int dirstack_next(struct dirstack *stack, char *fullname, struct stat *st_arg);
struct recur *recur_open(char **paths);
void recur_close(struct recur *recur);
int recur_next(struct recur *recur, char *name, struct stat *st_arg);

#endif /* MAGICRESCUE_RECUR_H */
