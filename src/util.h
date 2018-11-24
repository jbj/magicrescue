#ifndef MAGICRESCUE_UTIL_H
#define MAGICRESCUE_UTIL_H

#include <sys/types.h>
#include <limits.h>

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

struct string {
    char *s;
    size_t l;
};

int hex2num(char c);
long long hextoll(const char *str);
long atol_calc(const char *str);
off_t rich_seek(int fd, const char *string);

void string_init(struct string *dst, const char *src);
void string_destroy(struct string *string);

#endif /* MAGICRESCUE_UTIL_H */
