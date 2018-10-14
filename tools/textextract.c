/*
 * Magic Rescue, text extraction
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
#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static int max_score = 9;
static int block_score = 5;
static int max_line = 5*80;
static int max_reverse = 0;
static long max_bytes = 0;

static size_t bufsize = 8192;
static char *buf;

static short scorelut[UCHAR_MAX+1], replut[UCHAR_MAX+1];

struct rule {
    short score, rep;
    char *ranges;
};

enum direction { DIR_FORWARD, DIR_REVERSE };
static enum direction direction = DIR_FORWARD;

struct scores {
    char *last_letter;
    unsigned char repeated_char;
    int sum_score, sum_repeats, cur_line;
    off_t offset;
    long bytes_processed;
};

/**
 * Initializes s to default values. Sets s->offset to offset, which may be -1
 * for unknown.
 */
static void scores_init(struct scores *s, off_t offset)
{
    s->last_letter = NULL;
    s->repeated_char = '\0';
    s->sum_score = s->sum_repeats = s->cur_line = 0;
    s->offset = offset;
    s->bytes_processed = 0;
}

/**
 * Inspects a single char on the address p.
 *
 * Returns 0 for "keep going".
 * Returns 1 for "EOF found", in which case the EOF will be after
 * s->last_letter.
 */
static int inspect_char(struct scores *s, char *p)
{
    const unsigned char c = *p;
    int score, max_repeats;

    /* Handle block offset score */
    if (s->offset >= 0 && block_score > 0) {
	if ((s->offset & 511) == 0 && s->sum_score < block_score)
	    s->sum_score = block_score;
	s->offset += (direction == DIR_FORWARD ? 1 : -1);
    }
    
    /* Handle character score */
    score = scorelut[c];
    s->sum_score += score;
    if (s->sum_score < 0)
	s->sum_score = 0;
    if (score <= 0)
	s->last_letter = p;
    if (score > 0) {
	fprintf(stderr, "score +%d for 0x%02X\n", score, c);
    }

    if (s->sum_score > max_score) {
	fprintf(stderr, "Score too high at %lld\n", (long long)s->offset);
	fprintf(stderr, "%d > %d\n", s->sum_score, max_score);
	return 1;
    }

    /* Handle repeat */
    if (s->repeated_char == c && (max_repeats = replut[c])) {
	s->sum_repeats++;
	if (s->sum_repeats > max_repeats) {
	    fprintf(stderr, "Too many repeats of '%c' (0x%02X)\n", c, c);
	    fprintf(stderr, "%d > %d at %lld\n",
		    s->sum_repeats, max_repeats, (long long)s->offset);
	    return 1;
	}
    } else {
	s->sum_repeats = 0;
    }
    s->repeated_char = c;
    
    /* Handle line length */
    if (max_line > 0) {
	if (c == '\r' || c == '\n') {
	    s->cur_line = 0;
	} else if (++s->cur_line > max_line) {
	    fprintf(stderr, "Line too long at %lld\n", (long long)s->offset);
	    return 1;
	}
    }

    /* handle max bytes */
    if (max_bytes > 0 && ++s->bytes_processed > max_bytes) {
	fprintf(stderr, "Wrote max bytes\n");
	return 1;
    }

    return 0;
}

static void make_luts(void)
{
    /* see http://www.bbsinc.com/iso8859.html */
    struct rule *rule, rules[] = {
	/* default values */
	{  0, 120, "\x01-\xFF"},
	/* never used control characters */
	{  4, 0, "\x01-\x1F"  },
	/* 8-bit characters */
	{  1, 8, "\x80-\xFF"  },
	/* characters not in ISO 8859-1 */
	{  2, 0, "\x7F-\x9F"  },
	/* characters in Windows latin-1 */
	{  1, 0, "\x82-\x8C\x91-\x9C\x9F"  },
	/* rarely used control chars: EOF, bell, backspace, form feed, ESC */
	{  2, 0, "\x04\x07\x08\x0C\x1B" },
	/* the NUL character */
	{ 10, 0, ""           },
	/* 0xFF */
	{  3, 0, "\xFF"       },
	/* whitespace */
	{ -1, 180, " \t\r\n"  },
	/* English letters and numbers */
	{ -2, 80, "a-zA-Z0-9" },
	{  0,  0, NULL        }
    };

    for (rule = rules; rule->ranges != NULL; rule++) {
	unsigned char a, b;
        char *range = rule->ranges;
	int i;

	do {
	    a = b = *(range++);

	    if (a && *range == '-' && (b = range[1])) {
		range += 2;
	    }
	    for (i = a; i <= b; i++) {
		scorelut[i] = rule->score;
		replut[i]   = rule->rep;
	    }
	} while (a && *range);
    }
}

static ssize_t write_all(int fd, const void *ptr, size_t count)
{
    size_t written = 0;
    ssize_t rv;
    while (written < count) {
	rv = write(fd, (char *)ptr + written, count - written);
	if (rv < 0)
	    return -1;
	written += rv;
    }
    return written;
}

static void usage(void)
{
    fprintf(stderr,
"Usage: textextract [-r MAX_REVERSE] [-M MAX_BYTES] [-s MAX_SCORE]\n"
"       [-b BLOCK_SCORE] OUTPUT_FILE|-\n"
"\n"
"Tries to recognize human-readable text among binary junk.\n"
"Expects a file, preferably seekable, on standard intput. Writes to \n"
"OUTPUT_FILE, or stdout if it's \"-\".\n"
"\n"
"  -r  Read backwards to find beginning of file up to MAX_REVERSE bytes.\n"
"  -M  Set the max number of bytes to output. Default unlimited.\n"
"  -s  Set max score before quitting. [%d]\n"
"  -l  Set max line length, in bytes. [%d]\n"
"  -b  Assign this value to the score when crossing a block boundary. [%d]\n"
, max_score, max_line, block_score);
}

static int read_backward(struct scores *s, int outfd)
{
    ssize_t read_count;
    char *p;

    if (s->offset <= 0)
	return 0;

    read_count = bufsize;
    if (s->offset < bufsize)
	read_count = (ssize_t)s->offset;
    if (max_reverse < read_count)
	read_count = max_reverse;

    errno = 0;
    if (lseek(0, -read_count, SEEK_CUR) < 0
	    || read(0, buf, read_count) != read_count) {
	perror("Reading backwards");
	return -1;
    }

    p = buf+read_count-1;
    for (; p >= buf; p--) {
	if (inspect_char(s, p)) {
	    if (s->last_letter) {

		write_all(outfd, s->last_letter, 
			read_count - (s->last_letter - buf));
	    
	    }
	    return 0;
	}
    }

    write_all(outfd, buf, read_count);
    return 0;
}

static int read_forward(struct scores *s, int outfd)
{
    ssize_t read_count;
    char *p, *bufpos;

    bufpos = buf;

    while ((read_count = read(0, bufpos, bufsize - (bufpos-buf)) ) > 0) {
	for (p = bufpos; p-bufpos < read_count; p++) {
	    if (inspect_char(s, p)) {
		return 0;
	    }
	}
	bufpos = p;

	if (bufsize == (size_t)(bufpos-buf)) {
	    /* buffer full, flush to stdout */
	    if (!s->last_letter) {
		/* buffer not big enough, this shouldn't happen */
		fprintf(stderr, "textextract: internal error\n");
		return -1;
	    }
	    if (write_all(outfd, buf, s->last_letter - buf) <= 0) {
		perror("Write error");
		return -1;
	    }
	    memmove(buf, s->last_letter, bufpos - s->last_letter);
	    bufpos -= s->last_letter - buf;
	    s->last_letter = NULL;
	}
    }
    return 0;
}

static void do_textextract(int outfd)
{
    struct scores s;
    scores_init(&s, lseek(0, 0, SEEK_CUR));
    
    if (direction == DIR_REVERSE) {
	if (read_backward(&s, outfd) != 0)
	    return;
    }

    direction = DIR_FORWARD;
    scores_init(&s, s.offset);

    if (read_forward(&s, outfd) == 0 && s.last_letter) {
	write_all(outfd, buf, s.last_letter+1 - buf);
    }
}

int main(int argc, char **argv)
{
    /* TODO:
     * "stopstring" option for reverse operation, e.g. "#!/"
     * option for print-debug-info
     * option to add a different ruleset?
     */
    
    int c, outfd;

    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
	usage();
	return 1;
    }

    while ((c = getopt(argc, argv, "M:s:b:l:r:")) >= 0) {
	switch (c) {
	case 'M':
	    max_bytes = atol_calc(optarg);
	
	break; case 's':
	    max_score = atoi(optarg);

	break; case 'b':
	    block_score = atoi(optarg);

	break; case 'l':
	    max_line = atoi(optarg);

	break; case 'r':
	    max_reverse = atoi(optarg);
	    if (max_reverse <= 0) {
		fprintf(stderr, "Invalid argument to -r\n");
		return 1;
	    }
	    direction = DIR_REVERSE;

	break; default:
	    fprintf(stderr, "Error parsing options.\n");
	    usage();
	    return 1;
	}
    }

    if (strcmp(argv[optind], "-") == 0) {
	outfd = 1;
    } else if ((outfd = 
		open(argv[optind], O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
	fprintf(stderr, "textextract: opening %s: %s\n",
		argv[optind], strerror(errno));
	return 1;
    }

    if (bufsize < (size_t)max_reverse)
	bufsize = max_reverse;
    buf = malloc(bufsize);
    if (!buf) {
	fprintf(stderr, "Failed to allocate %u bytes of memory\n", bufsize);
	return 1;
    }

    make_luts();
    do_textextract(outfd);

    close(outfd);
    free(buf);
    return 0;
}

