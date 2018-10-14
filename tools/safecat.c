/*
 * Magic Rescue
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
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE

#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

static void usage(void)
{
    fprintf(stderr,
"Usage: safecat [-dut] FILENAME | COMMAND\n"
"Copies verbatim from standard input to standard output. Standard output is\n"
"expected to be piped into COMMAND to produce FILENAME. This file will be\n"
"checked for size, and if that size falls below certain limits the program\n"
"will exit.\n"
"\n"
"  -d Maximum difference between bytes in and out of the command\n"
"  -t Maximum bytes the command is allowed to produce\n"
"  -u Maximum bytes to pipe into a command that has stopped producing output\n"
"\n"
"  The values for the -dut options can be postfixed with G, M or K.\n"
);
}

static ssize_t atoi_calc(const char *str)
{
    if (str[0] != '\0') {
	ssize_t result = atoi(str);

	switch (str[strlen(str)-1]) {
	    case 'G': result *= 1024;
	    case 'M': result *= 1024;
	    case 'k':
	    case 'K': result *= 1024;
	}
    	return result;

    } else {
	return 0;
    }
}

int main(int argc, char **argv)
{
    ssize_t max_diff      =  5*1024*1024;
    ssize_t max_unchanged =  1*1024*1024;
    ssize_t max_total     = 50*1024*1024;

    char *ofile;
    const size_t blocksize = 102400;
    char *buf;
    int c;

    while ((c = getopt(argc, argv, "d:u:t:")) >= 0) {
	switch (c) {
	case 'd':
	    max_diff      = atoi_calc(optarg);
	break; case 'u':
	    max_unchanged = atoi_calc(optarg);
	break; case 't':
	    max_total     = atoi_calc(optarg);

	break; default:
	    fprintf(stderr, "Error parsing options.\n");
	    usage();
	    return 1;
	}
    }

    if (argc - optind != 1 || strcmp(argv[optind], "--help") == 0) {
	usage();
	return 1;
    }

    ofile = argv[optind];
    buf = malloc(blocksize);

    {
	off_t total_in = 0, total_out = 0, in_at_last_out = 0;
	struct stat st;
	ssize_t read_count, write_count;
	const char *buf_offset;

	do {
	    read_count = read(0, buf, blocksize);
	    if (read_count <= 0) break;

	    buf_offset = buf;
	    do {
		write_count = write(1, buf_offset, read_count);
		if (write_count <= 0) goto end_loop;

		read_count -= write_count;
		buf_offset += write_count;
		total_in   += write_count;

	    } while (read_count > 0);

	    if (stat(ofile, &st) == 0) {
		if (total_out != st.st_size) {
		    total_out = st.st_size;
		    in_at_last_out = total_in;
		}
	    } else {
		total_out = 0;
	    }
	} while (  max_total     > total_out
	        && max_diff      > total_in - total_out
	        && max_unchanged > total_in - in_at_last_out);
    }

end_loop:
    free(buf);
    return 0;
}

