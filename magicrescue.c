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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "config.h"

struct string {
	unsigned char *s;
	size_t l;
};

union param {
	struct string string;
	struct {
		uint32_t val;
		uint32_t mask;
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

typedef int (*op_function)(unsigned char *, union param *);

typedef unsigned char *(*scan_function)(unsigned char *, size_t,
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
	long min_output_file;
	char *name;
	int allow_overlap;
	off_t skip_bytes;
};

struct recipe *recipes;
int recipe_count;

char *output_dir = NULL;
int machine_output = 0;

size_t overlap = 0;
const size_t blocksize = 102400;
unsigned char *buf;
long file_index = 0;

int op_string(unsigned char *s, union param *p)
{
	return memcmp(s, p->string.s, p->string.l) == 0;
}

int op_int32(unsigned char *s, union param *p)
{
	return ((s[0]<<24 | s[1]<<16 | s[2]<<8 | s[3]) & p->int32.mask)
		== p->int32.val;
}


void string_destroy(struct string *string)
{
	free(string->s);
}

void op_destroy(struct operation *op)
{
	/* Primitive multi-dispatch.
	 * Add a case here for every operation type that requires destruction */
	if (op->func == op_string) {
		string_destroy(&op->param.string);
	}
}


unsigned char *scanner_char(unsigned char *scanbuf, size_t scanbuf_len,
		union scan_param *param)
{
	return memchr(scanbuf, param->c, scanbuf_len);
}

unsigned char *scanner_string(unsigned char *scanbuf, size_t scanbuf_len,
		union scan_param *param)
{
	unsigned char *p = scanbuf + param->scanstring.magicoff;
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

void scanner_destroy(struct scanner *scanner)
{
	/* Primitive multi-dispatch.
	 * Add a case here for every operation type that requires destruction */
	if (scanner->func == scanner_string) {
		string_destroy(&scanner->param.scanstring.string);
	}
}

/** Returns the value (0-15) of a single hex digit. Returns 0 on error. */
int hex2num(unsigned char c)
{
	return  (c >= '0' && c <= '9' ? c-'0' :
			(c >= 'a' && c <= 'f' ? c-'a'+10 :
			(c >= 'A' && c <= 'F' ? c-'A'+10 : 0)));
}

long long hextoll(const unsigned char *str)
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
void string_new(struct string *dst, const unsigned char *src)
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

/** Calls an external program to extract a file from fd */
off_t extract(int fd, long prefix, struct recipe *r)
{
	pid_t pid;
	char outfile[64];
	snprintf(outfile, sizeof outfile, "%s/%08ld.%s", 
			output_dir, prefix, r->extension);

	pid = fork();
	if (pid == 0) {
		dup2(fd, 0);
		if (machine_output)
			dup2(2, 1); /* send program's stdout to stderr */
		
		execl("/bin/sh", "/bin/sh", "-c", r->command, outfile, NULL);
		perror("Executing /bin/sh");
		exit(1);

	} else if (pid > 0) {
		struct stat st;
		wait(NULL);

		if (stat(outfile, &st) == 0) {
			if (st.st_size < r->min_output_file) {
				fprintf(stderr, "Output too small, removing\n");
				unlink(outfile);

			} else {
				if (machine_output)
					printf("o %lld %s\n", (long long int)st.st_size, outfile);
				else
					fprintf(stderr, "%s: %lld bytes\n", outfile, 
							(long long int)st.st_size);

				return st.st_size;
			}

		} else {
			fprintf(stderr, "No output file\n");
		}
	}

	return 0;
}

void scanner_string_init(union scan_param *param)
{
	unsigned char winner = '\0', c;
	int i, winner_off = 0;

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

/** Runs all recipes on scanbuf, one after the other. Returns 1 if an
 * external program has been called, which means the file position has changed.
 * Returns -1 on fatal error. */
int scan_buf(unsigned char *scanbuf, ssize_t scanbuf_len,
		int fd, off_t scanbuf_filepos)
{
	int i, j, has_matched = 0;

	for (i = 0; i < recipe_count; i++) {
		struct recipe *r = &recipes[i];
		unsigned char *p = scanbuf + r->scanner.offset;
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
						r->name, scanbuf_filepos + (p-scanbuf));

				if (lseek(fd, scanbuf_filepos + (p-scanbuf), SEEK_SET)
						== (off_t)-1) {
					perror("lseek()");
					return -1;
				}

				output_size = extract(fd, file_index++, r);
				has_matched = 1;

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
void scan_disk(char *device, off_t start_offset)
{
	ssize_t got;
	int fd = open(device, O_RDONLY);
	int result, firsttime = 1;

	unsigned char *readbuf = buf, *scanbuf = buf;
	size_t readsize = blocksize;
	off_t offset_before_read;
	
	if (fd == -1) {
		fprintf(stderr, "opening %s: %s\n", device, strerror(errno));
		return;
	}

	offset_before_read = lseek(fd, start_offset, SEEK_CUR);
	if (offset_before_read == (off_t)-1) {
		fprintf(stderr, "lseek to %lld failed on %s: %s\n",
				(long long int)start_offset, device, strerror(errno));
		return;
	}

	if (machine_output)
		printf("i %s\n", device);

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

		result = scan_buf(scanbuf, got + (readbuf - scanbuf) - overlap,
				fd, offset_before_read - (readbuf - scanbuf));
		if (result == -1)
			return;
		else if (result) {
			if (lseek(fd, offset_before_read + got, SEEK_SET) == (off_t)-1) {
				perror("lseek()");
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
}

void recipe_new(struct recipe *r)
{
	r->scanner.func      = NULL;
	r->scanner.offset    = 0;
	r->scanner.extra_len = 0;
	r->ops               = NULL;
	r->ops_count         = 0;
	r->extension[0]      = '\0';
	r->command           = NULL;
	r->min_output_file   = 100;
	r->name              = NULL;
	r->allow_overlap     = 0;
	r->skip_bytes        = 0;
}

void recipe_destroy(struct recipe *r)
{
	int i;
	for (i = 0; i < r->ops_count; i++) {
		op_destroy(&r->ops[i]);
	}
	scanner_destroy(&r->scanner);
	free(r->ops);
	free(r->command);
	free(r->name);
}

void recipes_free()
{
	int i;

	for (i = 0; i < recipe_count; i++) {
		recipe_destroy(&recipes[i]);
	}
	free(recipes);
}

int parse_recipe(char *recipefile)
{
	struct recipe *r;
	FILE *fd = NULL; 
	char **sp, *search_path[] = { ".", "recipes", NULL, NULL };

#ifdef RECIPE_PATH
	search_path[2] = RECIPE_PATH;
#endif
	for (sp = search_path; !fd && *sp; sp++) {
		char path[PATH_MAX];
		snprintf(path, sizeof path, "%s/%s", *sp, recipefile);
		fd = fopen(path, "r");
	}
	
	if (!fd) {
		fprintf(stderr, "Opening %s: %s\n", recipefile, strerror(errno));
		return 0;
	}

	recipes = realloc(recipes, ++recipe_count * sizeof(*recipes));
	r = &recipes[recipe_count - 1];
	recipe_new(r);

	{
		char *basename = strrchr(recipefile, '/');
		r->name = strdup(basename ? basename + 1 : recipefile);
	}

	while (fgets(buf, blocksize, fd)) {
		char opname[64];
		int magic_offset, param_offset;
		buf[strlen(buf) - 1] = '\0'; /* kill trailing newline */

		if (sscanf(buf, "%d %63s %n",
					&magic_offset, opname, &param_offset) >= 2) {
			size_t len = 0;
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
					} else
						return 0;
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
					if (sscanf(param, "%15s %15s", buf_val, buf_mask) != 2)
						return 0;
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

			} else if (sscanf(buf, "min_output_file %ld", &r->min_output_file)
					== 1) {
				/* do nothing */
			} else if (strcmp(buf, "allow_overlap") == 0) {
				r->allow_overlap = 1;
			} else {
				fprintf(stderr, "Invalid line in %s: %s\n", recipefile, buf);
				return 0;
			}

		}
	}

	if (!r->scanner.func || !r->command) {
		fprintf(stderr, 
"Invalid recipe file. It must contain one scanner directive, one output\n"
"directive and at least one match directive\n");
		return 0;
	}

	/* Round overlap to machine word boundary, for faster memcpy */
	if (overlap %  sizeof(long)) {
		overlap &= sizeof(long) - 1;
		overlap += sizeof(long);
	}

	return 1;
}

void usage()
{
	printf(
"Usage: magicrescue -d OUTPUT_DIR -r RECIPE1 [-r RECIPE2 [...]] \n"
"       [-M] DEVICE1 [DEVICE2 [...]]\n"
"\n"
"  -d  Output directory for found files. Mandatory.\n"
"  -r  Recipe name or file. At least one must be specified.\n"
"  -M  Produce machine-readable output to stdout\n"
"  -O  Resume from this offset (hex or decimal) in the first device\n"
"\n");
}

int main(int argc, char **argv)
{
	int c;
	off_t start_offset = 0;

	/* Some recipes depend on parsing error messages from various programs */
	setenv("LC_ALL", "C", 1);

	/* Some helper programs will segfault on malformed input. This should
	 * prevent core files from piling up */
	{
		struct rlimit rlp;
		rlp.rlim_cur = rlp.rlim_max = 0;
		setrlimit(RLIMIT_CORE, &rlp);
	}

	buf = malloc(blocksize);

	sprintf(buf, "%s:%s", getenv("PATH"), "commands");
#ifdef COMMAND_PATH
	strcat(buf, ":");
	strcat(buf, COMMAND_PATH);
#endif
	setenv("PATH", buf, 1);

	while ((c = getopt(argc, argv, "O:d:r:M")) >= 0) {
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
			machine_output = 1;
			setvbuf(stdout, NULL, _IOLBF, 0);

		break; case 'O':
			if (optarg[0] == '0' && optarg[1] == 'x') {
				start_offset = (off_t)hextoll(optarg + 2);
			} else {
				start_offset = (off_t)atoll(optarg);
			}

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

	while (optind < argc) {
		scan_disk(argv[optind++], start_offset);
		start_offset = 0;
	}

	recipes_free();
	free(buf);
	return 0;
}

/* vim: ts=4 sw=4 noet tw=80
 */
