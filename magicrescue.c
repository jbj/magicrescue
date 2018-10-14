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
#define _LARGEFILE64_SOURCE

#ifdef HAVE_GETRLIMIT
# include <sys/time.h>
# include <sys/resource.h>
#endif /* HAVE_GETRLIMIT */

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

struct string {
    unsigned char *s;
    size_t l;
};

union param {
    struct string string;
    struct {
	unsigned long val;
	unsigned long mask;
    } int32;
};

union scan_param {
    char c;
    struct {
	struct string string;
	char magicchar;
	int magicoff;
    } scanstring;
};

typedef int (*op_function)(const unsigned char *, union param *);

typedef const unsigned char *(*scan_function)(const unsigned char *, size_t,
	union scan_param *);

struct operation {
    op_function func;
    union param param;
    int offset;
};

struct scanner {
    scan_function func;
    union scan_param param;
    int offset;
    int extra_len;
};

struct recipe {
    struct scanner scanner;
    struct operation *ops;
    int ops_count;
    char extension[64];
    char *command;
    char *postextract;
    char *rename;
    long min_output_file;
    char *name;
    int allow_overlap;
    off_t skip_bytes;
};

static struct recipe *recipes;
static int recipe_count;

static char *output_dir = NULL;
static enum { OUT_HUMAN = 0, OUT_I = 1, OUT_O = 2, OUT_IO = 3 }
    machine_output = OUT_HUMAN;
static enum { MODE_DEVICE, MODE_FILES } name_mode = MODE_DEVICE;

static ssize_t overlap = 0;
static const size_t blocksize = 102400;
static unsigned char *buf;

static struct {
    off_t position;
    char device[PATH_MAX];
    char device_basename[PATH_MAX];
} progress;

static const unsigned char *scanner_string(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param);




static void signal_beforedeath(int signo)
{
    fprintf(stderr,
"\nmagicrescue: killed by signal %d at offset 0x%llX in file %s\n",
	    signo, (long long)progress.position, progress.device);

    if (progress.position)
	fprintf(stderr,
"See the manual for instructions on resuming from this offset later\n");

    exit(0);
}

static int op_string(const unsigned char *s, union param *p)
{
    return memcmp(s, p->string.s, p->string.l) == 0;
}

static int op_int32(const unsigned char *s, union param *p)
{
    return ((s[0]<<24 | s[1]<<16 | s[2]<<8 | s[3]) & p->int32.mask)
	== p->int32.val;
}


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

/** Initializes a struct string from a 0-terminated string, parsing escape
 * sequences. The struct string will not be 0-terminated. */
static void string_new(struct string *dst, const unsigned char *src)
{
    const size_t slen = strlen(src);
    size_t i;
    dst->l = 0;
    dst->s = malloc(slen);

    for (i = 0; i < slen; i++) {
	if (src[i] == '\\') {
	    if (src[i+1] == 'x' && i + 3 < slen) {
		dst->s[dst->l++] = (hex2num(src[i+2])<<4) | hex2num(src[i+3]);
		i += 3;

	    } else {
		dst->s[dst->l++] = (
			src[i+1] == 'a' ? '\a' : (
			src[i+1] == 'b' ? '\b' : (
			src[i+1] == 'f' ? '\f' : (
			src[i+1] == 'n' ? '\n' : (
			src[i+1] == 'r' ? '\r' : (
			src[i+1] == 't' ? '\t' : (
			src[i+1] == 'v' ? '\v' : (
			src[i+1] ))))))));
		i++;
	    }

	} else {
	    dst->s[dst->l++] = src[i];
	}
    }

    /* dst->s = realloc(dst->s, dst->l); // who cares? */
}

static void string_destroy(struct string *string)
{
    free(string->s);
}

static void op_destroy(struct operation *op)
{
    /* Primitive multi-dispatch.
     * Add a case here for every operation type that requires destruction */
    if (op->func == op_string) {
	string_destroy(&op->param.string);
    }
}

static void scanner_destroy(struct scanner *scanner)
{
    /* Primitive multi-dispatch.
     * Add a case here for every operation type that requires destruction */
    if (scanner->func == scanner_string) {
	string_destroy(&scanner->param.scanstring.string);
    }
}

static void recipe_new(struct recipe *r)
{
    r->scanner.func      = NULL;
    r->scanner.offset    = 0;
    r->scanner.extra_len = 0;
    r->ops               = NULL;
    r->ops_count         = 0;
    r->extension[0]      = '\0';
    r->command           = NULL;
    r->postextract       = NULL;
    r->rename            = NULL;
    r->min_output_file   = 100;
    r->name              = NULL;
    r->allow_overlap     = 0;
    r->skip_bytes        = 0;
}

static void recipe_destroy(struct recipe *r)
{
    int i;
    for (i = 0; i < r->ops_count; i++) {
	op_destroy(&r->ops[i]);
    }
    scanner_destroy(&r->scanner);
    free(r->ops);
    free(r->command);
    free(r->postextract);
    free(r->rename);
    free(r->name);
}

static void recipes_free(void)
{
    int i;

    for (i = 0; i < recipe_count; i++) {
	recipe_destroy(&recipes[i]);
    }
    free(recipes);
}


static const unsigned char *scanner_char(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param)
{
    return memchr(scanbuf, param->c, scanbuf_len);
}

static const unsigned char *scanner_string(const unsigned char *scanbuf,
	size_t scanbuf_len, union scan_param *param)
{
    const unsigned char *p = scanbuf + param->scanstring.magicoff;
    struct string string = param->scanstring.string;
    unsigned char magicchar = param->scanstring.magicchar;

    while (p - scanbuf < scanbuf_len &&
	    (p = memchr(p, magicchar, scanbuf_len - (p-scanbuf)))) {
	p -= param->scanstring.magicoff;
	if (memcmp(p, string.s, string.l) == 0) {
	    return p;
	}
	p += param->scanstring.magicoff + 1;
    }
    return NULL;
}

static void scanner_string_init(union scan_param *param)
{
    unsigned char winner = '\0', c;
    size_t i, winner_off = 0;

    /* This table represents the number of million occurrences of each character
     * on my 9GB sample partition. We want to select the character that has the
     * smallest score. */
    static const int scoretable[] = {
1747, 126, 57, 39, 66, 30, 26, 25, 63, 45, 91, 20, 35, 22, 22, 36, 33, 20, 21,
17, 25, 17, 16, 15, 23, 18, 16, 15, 21, 15, 15, 15, 318, 18, 40, 27, 86, 21, 23,
22, 36, 33, 28, 21, 38, 40, 50, 50, 94, 63, 45, 39, 43, 35, 34, 31, 44, 34, 30,
29, 43, 35, 41, 18, 29, 69, 32, 40, 59, 63, 39, 32, 27, 40, 20, 23, 41, 34, 38,
32, 44, 24, 40, 47, 47, 31, 26, 23, 24, 20, 25, 20, 25, 21, 16, 79, 18, 123, 45,
83, 79, 182, 130, 49, 54, 115, 24, 33, 86, 61, 107, 106, 63, 20, 111, 103, 151,
64, 36, 32, 37, 35, 20, 18, 21, 22, 15, 15, 29, 20, 15, 40, 21, 28, 15, 14, 17,
74, 14, 67, 16, 33, 14, 13, 28, 15, 14, 15, 16, 15, 13, 12, 14, 13, 14, 13, 15,
13, 14, 13, 17, 14, 13, 15, 16, 13, 13, 14, 15, 13, 14, 13, 14, 13, 13, 13, 15,
13, 12, 13, 16, 14, 18, 14, 18, 14, 14, 13, 15, 15, 14, 14, 28, 19, 16, 21, 22,
13, 17, 23, 15, 16, 13, 13, 14, 13, 14, 13, 19, 14, 16, 14, 14, 13, 13, 13, 17,
16, 13, 14, 14, 16, 14, 14, 18, 14, 14, 14, 15, 16, 13, 14, 39, 21, 13, 17, 20,
14, 14, 15, 18, 13, 13, 14, 16, 14, 17, 16, 19, 15, 16, 17, 21, 20, 24, 179 };

    for (i = 0; i < param->scanstring.string.l; i++) {
	c = param->scanstring.string.s[i];
	
	if (scoretable[c] < scoretable[winner]) {
	    winner = c;
	    winner_off = i;
	}
    }

    param->scanstring.magicchar = winner;
    param->scanstring.magicoff  = winner_off;
}

static void compose_name(char *name, off_t offset, const char *extension)
{
    struct stat st;
    long i = 0;

    do {
	if (name_mode == MODE_DEVICE) {
	    snprintf(name, PATH_MAX, "%s/%012llX-%ld.%s", 
		    output_dir, (long long)offset, i++, extension);

	} else /*if (name_mode == MODE_FILES)*/ {
	    snprintf(name, PATH_MAX, "%s/%s-%ld.%s", 
		    output_dir, progress.device_basename, i++, extension);
	}
    } while (lstat(name, &st) == 0);
}

static int run_shell(int fd, off_t offset, const char *command,
	const char *argument, int stdout_fd)
{
    pid_t pid;
    int status = -1;

    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
	perror("lseek()");
	return -1;
    }

    pid = fork();
    if (pid == 0) {

	dup2(fd, 0);
	if (stdout_fd >= 0)
	    dup2(stdout_fd, 1);
	else if (machine_output)
	    dup2(2, 1); /* send program's stdout to stderr */
	
	execl("/bin/sh", "/bin/sh", "-c", command, "sh", argument, NULL);
	perror("Executing /bin/sh");
	exit(1);

    } else if (stdout_fd >= 0) {
	return 0;
    } else if (pid > 0) {
	wait(&status);
    }
    return status;
}

static void rename_output(int fd, off_t offset, const char *command,
	char *origname)
{
    int pipes[2];
    char mvbuf[2*PATH_MAX]; /* it has to be semi-large */
    ssize_t got, has = 0;
    char *rename_pos = mvbuf;

    if (pipe(pipes) != 0 ||
	   run_shell(fd, offset, command, origname, pipes[1]) != 0)
	return;

    close(pipes[1]);
    while ((got = read(pipes[0], mvbuf + has, sizeof(mvbuf)-has - 1)) > 0) {
	has += got;
    }
    close(pipes[0]);
    wait(NULL);

    mvbuf[has] = '\0';

    if (has > 7 &&
	    (strncmp(rename_pos, "RENAME ", 7) == 0 ||
	     (fprintf(stderr, "Warning: garbage on rename stdout\n"), 0) ||
	     ((rename_pos = strstr(mvbuf, "\nRENAME ")) && rename_pos++)||
	     (rename_pos = strstr(mvbuf, "RENAME ")))
       ) {
	char *nlpos;
	rename_pos += 7;
	if ((nlpos = strchr(rename_pos, '\n')))
	    *nlpos = '\0';

	if (strlen(rename_pos) < 128) {
	    char newname[PATH_MAX];
	    compose_name(newname, offset, rename_pos);

	    rename(origname, newname);
	    strcpy(origname, newname);
	}
    }
}

/** Calls an external program to extract a file from fd */
static off_t extract(int fd, struct recipe *r, off_t offset)
{
    char outfile[PATH_MAX];
    struct stat st;

    compose_name(outfile, offset, r->extension);

    if (run_shell(fd, offset, r->command, outfile, -1) == -1)
	return -1;

    if (stat(outfile, &st) == 0) {
	if (st.st_size < r->min_output_file) {
	    fprintf(stderr, "Output too small, removing\n");
	    unlink(outfile);

	} else {
	    if (r->rename) {
		rename_output(fd, offset, r->rename, outfile);
	    }

	    if (r->postextract) {
		run_shell(fd, offset, r->postextract, outfile, -1);
	    }

	    if ((machine_output & OUT_IO) == OUT_IO)
		printf("o %s\n", outfile);
	    else if (machine_output & OUT_O)
		printf("%s\n", outfile);
	    else
		fprintf(stderr, "%s: %lld bytes\n", outfile, 
			(long long int)st.st_size);

	    return st.st_size;
	}

    } else {
	fprintf(stderr, "No output file\n");
    }

    return 0;
}

/** Runs all recipes on scanbuf, one after the other. Returns 1 if an
 * external program has been called, which means the file position has changed.
 * Returns -1 on fatal error. */
static int scan_buf(const unsigned char *scanbuf, ssize_t scanbuf_len,
	int fd, off_t scanbuf_filepos)
{
    int i, j, has_matched = 0;

    for (i = 0; i < recipe_count; i++) {
	struct recipe *r = &recipes[i];
	const unsigned char *p = scanbuf + r->scanner.offset;
	struct operation *op;

	if (r->skip_bytes > scanbuf_len) {
	    r->skip_bytes -= scanbuf_len;
	    continue;
	} else if (r->skip_bytes > 0) {
	    p += r->skip_bytes;
	}

	while (p - scanbuf < scanbuf_len &&
		(p = r->scanner.func(p, scanbuf_len - (p-scanbuf) +
				     r->scanner.extra_len,
				     &r->scanner.param))) {

	    p -= r->scanner.offset;
	    for (j = 0; j < r->ops_count; j++) {
		op = &r->ops[j];
		if (!op->func(p + op->offset, &op->param)) {
		    break;
		}
	    }

	    if (j == r->ops_count) {
		off_t output_size;

		fprintf(stderr, "Found %s at 0x%llX\n",
			r->name, (long long)scanbuf_filepos + (p-scanbuf));

		output_size = extract(fd, r, scanbuf_filepos + (p-scanbuf));
		has_matched = 1;

		if (output_size < 0)
		    return -1;

		if (output_size && !r->allow_overlap) {
		    r->skip_bytes = output_size - (scanbuf_len - (p-scanbuf));
		    if (r->skip_bytes < 0) {
			p += output_size - 1;
		    } else {
			break;
		    }
		}
	    }

	    p += r->scanner.offset + 1;
	}
    }

    return has_matched;
}

/** Opens and scans an entire device for matches with the selected recipes. */
static void scan_disk(const char *device)
{
    ssize_t got;
    int fd = 0;
    int result, firsttime = 1;

    unsigned char *readbuf = buf, *scanbuf = buf;
    size_t readsize = blocksize;
    off_t offset_before_read;
    
    if (strcmp(device, "-") != 0 &&
	    (fd = open(device, O_RDONLY)) == -1) {
	fprintf(stderr, "opening %s: %s\n", device, strerror(errno));
	return;
    }

    offset_before_read = lseek(fd, 0, SEEK_CUR);
    if (offset_before_read == (off_t)-1) {
	fprintf(stderr, "lseek failed on %s: %s\n", device, strerror(errno));
	close(fd);
	return;
    }

    snprintf(progress.device, sizeof progress.device, "%s", device);
    {
	char *tmp = strrchr(device, '/');
	snprintf(progress.device_basename, sizeof progress.device_basename, 
		"%s", tmp ? tmp+1 : device);
    }

    if ((machine_output & OUT_IO) == OUT_IO)
	printf("i %s\n", device);
    else if (machine_output & OUT_I)
	printf("%s\n", device);

    /* In the first iteration, the scan buffer and read buffer overlap like
     * this:
     * SSSSSSSSSSSSSSSSS
     * RRRRRRRRRRRRRRRRRRRRR
     *
     * For the rest of the iterations they overlap like this:
     * SSSSSSSSSSSSSSSSS
     *     RRRRRRRRRRRRRRRRR
     *
     * After each iteration the unscanned part of readbuf will be copied to the
     * start of scanbuf, and new data will be read into readbuf */

    while ((got = read(fd, readbuf, readsize)) > overlap) {
	progress.position = offset_before_read;

	result = scan_buf(scanbuf, got + (readbuf - scanbuf) - overlap,
		fd, offset_before_read - (readbuf - scanbuf));
	if (result == -1) {
	    close(fd);
	    return;

	} else if (result) {
	    if (lseek(fd, offset_before_read + got, SEEK_SET) == (off_t)-1) {
		perror("lseek()");
		close(fd);
		return;
	    }
	}

	memcpy(scanbuf, readbuf + got - overlap, overlap);

	if (firsttime) {
	    firsttime = 0;
	    readbuf  += overlap;
	    readsize -= overlap;
	}

	offset_before_read += got;
    }

    if (got == -1) {
	fprintf(stderr, "Read error on %s at %lld bytes: %s\n",
		device, (long long int)offset_before_read, strerror(errno));

	if (errno == EIO) {
	    fprintf(stderr, "Note that on some block devices this just "
		    "means end of file.\n");
	}
    } else {
	fprintf(stderr, "Scanning %s finished at %lldMB\n",
		device, (long long int)(offset_before_read >> 20));
    }

    close(fd);
    {
	int i;
	for (i = 0; i < recipe_count; i++) {
	    recipes[i].skip_bytes = 0;
	}
    }
}

static int parse_recipe(const char *recipefile)
{
    struct recipe *r;
    FILE *fh = NULL; 
    char **sp, *search_path[] = { ".", "recipes", NULL, NULL };

#ifdef RECIPE_PATH
    search_path[2] = RECIPE_PATH;
#endif
    for (sp = search_path; !fh && *sp; sp++) {
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", *sp, recipefile);
	fh = fopen(path, "r");
    }
    
    if (!fh) {
	fprintf(stderr, "Opening %s: %s\n", recipefile, strerror(errno));
	return 0;
    }

    recipes = realloc(recipes, ++recipe_count * sizeof(*recipes));
    r = &recipes[recipe_count - 1];
    recipe_new(r);

    {
	const char *basename = strrchr(recipefile, '/');
	r->name = strdup(basename ? basename + 1 : recipefile);
    }

    while (fgets(buf, blocksize, fh)) {
	char opname[64];
	int magic_offset, param_offset;
	buf[strlen(buf) - 1] = '\0'; /* kill trailing newline */

	if (sscanf(buf, "%d %63s %n",
		    &magic_offset, opname, &param_offset) >= 2) {
	    ssize_t len = 0;
	    char *param = buf + param_offset;
	    
	    if (!r->scanner.func) {
		/* Try to make it the scanner function */
		
		if (strcmp(opname, "string") == 0
			|| strcmp(opname, "char") == 0) {
		    struct string string;
		    string_new(&string, buf + param_offset);
		    r->scanner.offset = magic_offset;

		    if (string.l == 1) {
			r->scanner.func = scanner_char;
			r->scanner.param.c = string.s[0];
			string_destroy(&string);
			len = 1;
		    } else if (string.l > 1) {
			r->scanner.func = scanner_string;
			r->scanner.param.scanstring.string = string;
			scanner_string_init(&r->scanner.param);
			len = string.l;
		    } else {
			fclose(fh);
			return 0;
		    }
		}

		if (len) {
		    r->scanner.extra_len = len - 1;
		}
	    }

	    if (len <= 0) {
		/* If it did not become a scanner function it will become an
		 * operation */
		struct operation *op;

		r->ops = realloc(r->ops, ++r->ops_count * sizeof(*r->ops));
		op = &r->ops[r->ops_count - 1];

		op->offset = magic_offset;

		if (strcmp(opname, "string") == 0
			|| strcmp(opname, "char") == 0) {
		    op->func = op_string;
		    string_new(&op->param.string, param);
		    len = op->param.string.l;

		} else if (strcmp(opname, "int32") == 0) {
		    char buf_val[16], buf_mask[16];
		    op->func = op_int32;
		    if (sscanf(param, "%15s %15s", buf_val, buf_mask) != 2) {
			fclose(fh);
			return 0;
		    }
		    op->param.int32.val  = hextoll(buf_val);
		    op->param.int32.mask = hextoll(buf_mask);
		    op->param.int32.val &= op->param.int32.mask;
		    len = 4;

		} /*
		     To add more operations:
		     - Add another type to union param if you have to.
		     - Define a test function like op_string that returns 1 on
		       success and 0 on faliure.
		     - Add another "else if" clause here. It must assign to
		       op->func and to len.
		     - The value assigned to len should be the maximum number of
		       bytes your test function might read. Never read more
		       bytes than this.
		     - Add a case to op_destroy of cleanup is needed
		     The test functions you add should be general and reusable,
		     such as string, int, md5, regexp. To make them specific
		     they should take a parameter.
		*/

	    }

	    if (len <= 0) {
		fprintf(stderr, "Invalid operation '%s'\n", opname);
		fclose(fh);
		return 0;
	    }
	    len += magic_offset;
	    if (overlap < len)
		overlap = len;

	} else if (buf[0] != '#' && buf[0] != '\0') {

	    if (sscanf(buf, "extension %63s", r->extension) == 1) {
		/* do nothing */

	    } else if (strncmp(buf, "command ", 8) == 0) {
		r->command = strdup(buf + 8);

	    } else if (strncmp(buf, "postextract ", 12) == 0) {
		r->postextract = strdup(buf + 12);

	    } else if (strncmp(buf, "rename ", 7) == 0) {
		r->rename = strdup(buf + 7);

	    } else if (sscanf(buf, "min_output_file %ld", &r->min_output_file)
		    == 1) {
		/* do nothing */
	    } else if (strcmp(buf, "allow_overlap") == 0) {
		r->allow_overlap = 1;
	    } else {
		fprintf(stderr, "Invalid line in %s: %s\n", recipefile, buf);
		fclose(fh);
		return 0;
	    }

	}
    }

    fclose(fh);

    if (!r->scanner.func || !r->command) {
	fprintf(stderr, 
"Invalid recipe file. It must contain one scanner directive, one output\n"
"directive and at least one match directive\n");
	return 0;
    }

    /* Round overlap to machine word boundary, for faster memcpy */
    overlap +=   sizeof(long) - 1;
    overlap &= ~(sizeof(long) - 1);

    return 1;
}

static void usage(void)
{
    printf(
"Usage: magicrescue -d OUTPUT_DIR -r RECIPE1 [-r RECIPE2 [...]] \n"
"\t[-M MODE] DEVICE1 [DEVICE2 [...]]\n"
"\n"
"  -d  Output directory for found files. Mandatory.\n"
"  -r  Recipe name or file. At least one must be specified.\n"
"  -M  Produce machine-readable output to stdout\n"
"\n");
}

int main(int argc, char **argv)
{
    int c;

    /* Some recipes depend on parsing error messages from various programs */
    putenv("LC_ALL=C");

#ifdef HAVE_GETRLIMIT
    /* Some helper programs will segfault or exhaust memory on malformed
     * input. This should prevent core files and swap storms */
    {
	struct rlimit rlp;
	rlp.rlim_cur = rlp.rlim_max = 0;
	setrlimit(RLIMIT_CORE, &rlp);

	if (getrlimit(RLIMIT_AS, &rlp) == 0 && (
		    rlp.rlim_max == RLIM_INFINITY ||
		    rlp.rlim_max > MAX_MEMORY)) {
	    rlp.rlim_cur = rlp.rlim_max = MAX_MEMORY;

	    setrlimit(RLIMIT_AS, &rlp);
	}
    }
#endif /* HAVE_GETRLIMIT */

    progress.position = 0;
    progress.device[0] = progress.device_basename[0] = '\0';

    buf = malloc(blocksize);

    sprintf(buf, "PATH=%s:%s", getenv("PATH"), "tools");
#ifdef COMMAND_PATH
    sprintf(buf + strlen(buf), ":%s", COMMAND_PATH);
#endif
    putenv(strdup(buf));

    while ((c = getopt(argc, argv, "d:r:M:")) >= 0) {
	switch (c) {
	case 'd': {
	    struct stat dirstat;
	    if (stat(optarg, &dirstat) == 0 && dirstat.st_mode & S_IFDIR) {
		output_dir = optarg;
	    } else {
		fprintf(stderr, "Invalid directory %s\n", optarg);
		return 1;
	    }
	}
	break; case 'r':
	    if (!parse_recipe(optarg)) {
		fprintf(stderr, "Fatal error parsing %s\n", optarg);
		return 1;
	    }

	break; case 'M':
	    if (strchr(optarg, 'i'))
		machine_output |= OUT_I;
	    if (strchr(optarg, 'o'))
		machine_output |= OUT_O;
	    setvbuf(stdout, NULL, _IOLBF, 0);

	break; default:
	    fprintf(stderr, "Error parsing options.\n");
	    usage();
	    return 1;
	}
    }

    if (recipe_count == 0 || !output_dir) {
	usage();
	return 1;
    }

    name_mode = argc - optind < 10 ? MODE_DEVICE : MODE_FILES;

    signal(SIGINT,  signal_beforedeath);
    signal(SIGTERM, signal_beforedeath);
    signal(SIGSEGV, signal_beforedeath);
    signal(SIGPIPE, signal_beforedeath);
    
    while (optind < argc) {
	scan_disk(argv[optind++]);
    }

    recipes_free();
    free(buf);
    return 0;
}

/* vim: ts=8 sw=4 noet tw=80
 */
