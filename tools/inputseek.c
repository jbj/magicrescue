#include "config.h"

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

int main(int argc, char **argv)
{
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

    if (rich_seek(0, argv[1]) == (off_t)-1) {
	perror("lseek on stdin");
	return 1;
    }

    execvp(argv[2], &argv[2]);
    perror("exec");
    return 1;
}

