#ifndef _RECUR_H

#include <sys/stat.h>
#include <dirent.h>

#define RECUR_MAXDEPTH 100

struct dirstack {
    DIR *dirs[RECUR_MAXDEPTH];
    DIR **pos;
    char prefix[PATH_MAX];
};

struct recur {
    char **list;
    struct dirstack *stack;
};

void rm_rf(const char *dir);
struct dirstack *dirstack_open(const char *path);
void dirstack_close(struct dirstack *stack);
int dirstack_next(struct dirstack *stack, char *fullname, struct stat *st_arg);
struct recur *recur_open(char **paths);
void recur_close(struct recur *recur);
int recur_next(struct recur *recur, char *name, struct stat *st_arg);

# define _RECUR_H
#endif
