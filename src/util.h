#ifndef _UTIL_H

#include <sys/types.h>
#include <limits.h>

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

struct string {
    unsigned char *s;
    size_t l;
};

int hex2num(unsigned char c);
long long hextoll(const unsigned char *str);
off_t rich_seek(int fd, const char *string);

void string_init(struct string *dst, const unsigned char *src);
void string_destroy(struct string *string);

#define _UTIL_H
#endif /* _UTIL_H */
