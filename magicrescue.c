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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

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

typedef int (*op_function)(unsigned char *, union param *);

struct operation {
	op_function func;
	union param param;
	int offset;
};

struct recipe {
	struct operation *ops;
	int ops_count;
	int offset;
	char extension[64];
	char *command;
	int min_output_file;
	char *name;
};

struct recipe *recipes[256];
int recipes_count[256];
unsigned char char_list[256];
int char_list_count = 0;

char *output_dir = NULL;
int machine_output = 0;

size_t overlap = 0;
const size_t blocksize = 102400;
unsigned char *buf;
int file_index = 0;


void string_destroy(struct string *string)
{
	free(string->s);
}

int op_string(unsigned char *s, union param *p)
{
	return memcmp(s, p->string.s, p->string.l) == 0;
}

int op_int32(unsigned char *s, union param *p)
{
	return ((s[0]<<24 | s[1]<<16 | s[2]<<8 | s[3]) & p->int32.mask)
		== p->int32.val;
}

void op_destroy(struct operation *op)
{
	/* Primitive multi-dispatch.
	 * Add a case here for every operation type that requires destruction */
	if (op->func == op_string) {
		string_destroy(&op->param.string);
	}
}

#define HEX2NUM(c) ( \
        ((c) >= '0' && (c) <= '9' ? (c)-'0' : \
        ((c) >= 'a' && (c) <= 'f' ? (c)-'a'+10 : \
        ((c) >= 'A' && (c) <= 'F' ? (c)-'A'+10 : -1))) \
)

void string_new(struct string *dst, const unsigned char *src)
{
	const size_t slen = strlen(src);
	size_t i;
	dst->l = 0;
	dst->s = malloc(slen);

	for (i = 0; i < slen; i++) {
		if (src[i] == '\\') {
			if (src[i+1] == 'x' && i + 3 < slen) {
				dst->s[dst->l++] = (HEX2NUM(src[i+2])<<4) | HEX2NUM(src[i+3]);
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

void convert(int f, int prefix, struct recipe *r)
{
	pid_t pid;
	char outfile[64];
	snprintf(outfile, sizeof outfile, "%s/%08d.%s", 
			output_dir, prefix, r->extension);

	pid = fork();
	if (pid == 0) {
		dup2(f, 0);
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

			} else if (sizeof(off_t) == sizeof(long long int)) {
				if (machine_output)
					printf("o %lld %s\n", st.st_size, outfile);
				else
					fprintf(stderr, "%s: %lld bytes\n", outfile, st.st_size);
			}

		} else {
			fprintf(stderr, "No output file\n");
		}
	}
}

int scan_buf(int f, unsigned char *scanbuf, ssize_t got, off_t store_offset)
{
	int i, j, ok;
	struct operation *op;

	for (i = 0; i < char_list_count; i++) {
		unsigned char magic_char = char_list[i];
		unsigned char *p = scanbuf;

		while (p - scanbuf < got - overlap &&
				(p = memchr(p, magic_char, got - (p-scanbuf) - overlap))) {

			for (j = 0; j < recipes_count[*p]; j++) {
				struct recipe *r = &recipes[*p][j];

				if (r->offset > (p-scanbuf))
					continue;

				p -= r->offset;
				ok = 1;
				for (op = r->ops; op->func; op++) {
					if (!op->func(p + op->offset, &op->param)) {
						ok = 0;
						break;
					}
				}

				if (ok) {
					fprintf(stderr, "Found %s at 0x%llX\n",
							r->name, store_offset+(p-buf)-overlap);

					if (lseek(f, store_offset+(p-buf)-overlap, SEEK_SET)
							== (off_t)-1) {
						perror("lseek()");
						return 0;
					}

					convert(f, file_index++, r);

					if (lseek(f, store_offset + got, SEEK_SET) == (off_t)-1) {
						perror("lseek()");
						return 0;
					}

				}

				p += r->offset;
			}

			p++;
		}
	}
	return 1;
}

void scan_disk(char *device)
{
	ssize_t got;
	int f = open(device, O_RDONLY);
	int firsttime = 1;

	unsigned char *readbuf  = buf + overlap, *scanbuf  = buf + overlap;
	size_t readsize = blocksize - overlap;
	
	if (f == -1) {
		fprintf(stderr, "opening %s: %s\n", device, strerror(errno));
		return;
	}
	if (machine_output)
		printf("i %s\n", device);

	while (1) {
		off_t store_offset = lseek(f, 0, SEEK_CUR);
		if (store_offset == (off_t)-1) {
			fprintf(stderr, "lseek failed on %s: %s\n",
					device, strerror(errno));
			return;
		}

		got = read(f, readbuf, readsize);
		if (got == -1) {
			fprintf(stderr, "Read error %s at %lld bytes: %s\n",
					device, store_offset, strerror(errno));
			if (errno == EIO)
				fprintf(stderr,
"Note that on some block devices this just means end of file.\n");

			return;
		}
		if (got < overlap)
			break;

		if (!scan_buf(f, scanbuf, got, store_offset))
			return;

		if (got == readsize) {
			/* copy the last part of the buffer to the first part of the buffer,
			 * except on the very last iteration */
			memcpy(buf, buf + (blocksize-overlap), overlap);
		}

		if (firsttime) {
			firsttime = 0;
			scanbuf  -= overlap;
		}
	};

}

void recipe_new(struct recipe *r)
{
	r->ops = malloc(sizeof(struct operation));
	r->ops_count = 0;
	r->offset = -1;
	r->extension[0] = '\0';
	r->command = NULL;
	r->min_output_file = 100;
	r->name = NULL;
}

void recipe_destroy(struct recipe *r)
{
	int i;
	for (i = 0; i < r->ops_count; i++) {
		op_destroy(&r->ops[i]);
	}
	free(r->ops);
	free(r->command);
	free(r->name);
}

void recipes_free()
{
	int i, j;

	for (i = 0; i < char_list_count; i++) {
		unsigned char c = char_list[i];

		for (j = 0; j < recipes_count[c]; j++) {
			recipe_destroy(&recipes[c][j]);
		}
		free(recipes[c]);
	}
}

int parse_recipe(char *recipefile)
{
	int i = 0;
	struct recipe *r = NULL;
	FILE *fd = fopen(recipefile, "r");
	
	if (!fd) {
		fprintf(stderr, "Opening %s: %s\n", recipefile, strerror(errno));
		return 0;
	}

	while (fgets(buf, blocksize, fd)) {
		char opname[64];
		int magic_offset, param_offset;
		buf[strlen(buf) - 1] = '\0'; /* kill trailing newline */

		if (sscanf(buf, "%d %63s %n",
					&magic_offset, opname, &param_offset) >= 2) {
			
			/*fprintf(stderr, "offset %d, op '%s'\n", magic_offset,
					opname);*/
			if (!r && strcmp(opname, "char") == 0) {
				struct string string;
				unsigned char c;
				char *basename;

				string_new(&string, buf + param_offset);
				c = string.s[0];
				string_destroy(&string);

				if (recipes_count[c] == 0)
					char_list[char_list_count++] = c;

				recipes[c] = realloc(recipes[c], ++recipes_count[c] *
						sizeof(struct recipe));

				r = &recipes[c][recipes_count[c]-1];
				recipe_new(r);
				r->offset = magic_offset;

				basename = strrchr(recipefile, '/');
				r->name = strdup(basename ? basename + 1 : recipefile);

				/*fprintf(stderr, "char: '%c'\n", magic_char);*/
			
			} else {
				size_t len = 0;
				char *param = buf + param_offset;
				struct operation *op = &r->ops[i];

				if (!r) {
					fprintf(stderr,
							"The first recipe line must be a char line\n");
					return 0;
				}
				
				op->offset = magic_offset;

				if (strcmp(opname, "string") == 0) {
					op->func = op_string;
					string_new(&op->param.string, param);
					len = op->param.string.l;
					/*fprintf(stderr, "string: %*s\n",
							op->param.string.l, op->param.string.s);*/

				} else if (strcmp(opname, "int32") == 0) {
					op->func = op_int32;
					if (sizeof(unsigned int) == 4) {
						/*TODO: parse it myself*/
						sscanf(param, "%x %x", &op->param.int32.val,
								&op->param.int32.mask);
						op->param.int32.val &= op->param.int32.mask;
					} else {
						fprintf(stderr, "Sorry, unsupported CPU\n");
						return 0;
					}
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
					 The test functions you add should be general and reusable,
					 such as string, int, md5, regexp. To make them specific
					 they should take a parameter.
				*/

				if (len <= 0) {
					fprintf(stderr, "Internal error: no length\n");
					return 0;
				}
				len += magic_offset;
				if (overlap < len)
					overlap = len;

				i++;
				r->ops = realloc(r->ops, (i+1) * sizeof(struct operation));
			}

		} else if (buf[0] != '#' && buf[0] != '\0') {
			if (!r) {
				fprintf(stderr, "The first recipe line must be a char line\n");
				return 0;
			}
				
			if (sscanf(buf, "extension %63s", r->extension) == 1) {
				/* do nothing */

			} else if (strncmp(buf, "command ", 8) == 0) {
				r->command = strdup(buf + 8);

			} else if (sscanf(buf, "min_output_file %d", &r->min_output_file)
					== 1) {
				/* do nothing */
			} else {
				fprintf(stderr, "Invalid line in %s: %s\n", recipefile, buf);
				return 0;
			}

		}
	}

	r->ops[i].func = NULL;

	if (r->offset == -1 || !r->command || i == 0) {
		fprintf(stderr, 
"Invalid recipe file. It must contain one char directive, one output\n"
"directive and at least one match directive\n");
		return 0;
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
"  -r  Recipe file. At least one must be specified.\n"
"  -M  Produce machine-readable output to stdout\n"
"\n");
}

int main(int argc, char **argv)
{
	int c, i;

	buf = malloc(blocksize);

	/* hm, do I need to initialize static arrays manually, or is every element
	 * guaranteed to be 0? */
	memset(recipes_count, 0, sizeof recipes_count);
	for (i = 0; i < sizeof(recipes)/sizeof(*recipes); i++) {
		recipes[i] = NULL;
	}

	while ((c = getopt(argc, argv, "d:r:M")) >= 0) {
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

		break; default:
			fprintf(stderr, "Error parsing options.\n");
			usage();
			return 1;
		}
	}

	if (char_list_count == 0 || !output_dir) {
		usage();
		return 1;
	}

	while (optind < argc) {
		scan_disk(argv[optind++]);
	}

	recipes_free();
	free(buf);
	return 0;
}

/* vim: ts=4 sw=4 noet tw=80
 */
