#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

/** Returns the value (0-15) of a single hex digit. Returns 0 on error. */
static int hex2num(unsigned char c)
{
    return  (c >= '0' && c <= '9' ? c-'0' :
	    (c >= 'a' && c <= 'f' ? c-'a'+10 :
	    (c >= 'A' && c <= 'F' ? c-'A'+10 : 0)));
}

static long long hextoll(const unsigned char *str)
{
    long long result = 0;
    size_t i, len = strlen(str);

    for (i = 0; i < len; i++) {
	result |= (long long)hex2num(str[i]) << 4*((len-1) - i);
    }
    return result;
}

int main(int argc, char **argv)
{
    off_t offset;
    int whence = SEEK_SET;
    
    if (argc < 3) {
	fprintf(stderr,
"Usage: inputseek [+-=][0x]OFFSET COMMAND [ARG1 [ARG2 ...]]\n"
"\n"
"  Where OFFSET is the byte position to seek standard input to. I can be\n"
"  written in hex or decimal, and may include a prefix:\n"
"    =  Seek to an absolute position (default)\n"
"    +  Seek forward to a relative position\n"
"    -  Seek to EOF, minus the offset\n"
);
	return 1;
    }

    {
	const char *stroffset = argv[1];
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
	    fprintf(stderr, "Get a C99 compiler or specify offset in hex\n");
	    return 1;
#endif /* HAVE_ATOLL */
	}
	offset *= sign;
    }

    if (lseek(0, offset, whence) == (off_t)-1) {
	perror("lseek on stdin");
	return 1;
    }

    execvp(argv[2], &argv[2]);
    perror("exec");
    return 1;
}

