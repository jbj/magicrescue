/*
 * Magic Rescue main program
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

#ifdef HAVE_GETRLIMIT
# include <sys/time.h>
# include <sys/resource.h>
#endif /* HAVE_GETRLIMIT */

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "array.h"
#include "recipe.h"
#include "scanner.h"
#include "magicrescue.h"

struct range {
    off_t begin;
    off_t end;
};

/* An array of struct scanner.  Each scanner can contain one or more recipes. */
static struct array scanners;

/* An array of struct range.  Used by the anti-overlap code to list ranges that
 * have already been extracted and should not be extracted again.
 * A value of begin < 0 means that a slot is unused. */
static struct array skip_ranges;

char *output_dir = NULL;
enum OUTPUT_MODE machine_output = OUT_HUMAN;
enum NAME_MODE name_mode = MODE_DEVICE;

/* Structure containing the current input file name and position */
struct progress progress;

/* This value is the max length of a chunk of bytes to be looked at when
 * scanning.  It is constant after all recipes have been parsed. */
static ssize_t overlap = 0;

/* General-purpose buffer.  Mostly used to hold the raw data when scanning. */
static unsigned char *buf;
static const size_t bufsize = 102400;


/* Signal handler for fatal signals */
static void signal_beforedeath(int signo)
{
    fprintf(stderr,
"\nmagicrescue: killed by signal %d at offset 0x%llX in file %s\n",
	    signo, (long long)progress.position, progress.device);

    if (progress.position)
	fprintf(stderr,
"Use the -O option to resume from this offset later\n");

    exit(0);
}

/* Add a scanner to the global scanners list, or reuses an existing entry if
 * an identical one exists.
 * Returns a pointer to the added scanner. */
static struct scanner *scanner_add_or_reuse(struct scanner *scanner)
{
    struct scanner *el;

    array_foreach(&scanners, el) {
	if (scanner_compare(scanner, el) == 0) {
	    scanner_destroy(scanner);
	    return el;
	}
    }

    return array_add(&scanners, scanner);
}

/* Free the global scanners list. */
static void scanners_free(void)
{
    struct scanner *scanner;

    array_foreach(&scanners, scanner) {
	scanner_destroy(scanner);
    }
    array_destroy(&scanners);
}

/* Scans scanbuf with all scanners in the global list, testing all recipes on
 * them.
 * Returns 1 if an external program has been called, which means the file
 * position has changed.  Returns -1 on fatal error. */
static int scan_buf(const unsigned char *scanbuf, ssize_t scanbuf_len,
	int fd, off_t scanbuf_filepos)
{
    int filepos_changed = 0;
    struct scanner *scanner;
    struct recipe *r;
    struct range *range;

    /* clean up ranges we have gone past, and see if we can skip some of the
     * buffer */
    array_foreach(&skip_ranges, range) {
	if (range->begin < 0) continue;

	if (range->end < scanbuf_filepos) {
	    range->begin = -1;

	} else if (range->end > scanbuf_filepos + scanbuf_len) {
	    return 0;

	} else if (range->begin <= scanbuf_filepos
		&& range->end > scanbuf_filepos) {
	    scanbuf         += range->end - scanbuf_filepos;
	    scanbuf_len     -= range->end - scanbuf_filepos;
	    scanbuf_filepos  = range->end;
	}
    }

    array_foreach(&scanners, scanner) {
	const unsigned char *p = scanbuf + scanner->offset;

	while (p - scanbuf < scanbuf_len &&
		(p = scanner->func(p, scanbuf_len - (p-scanbuf) +
				     scanner->extra_len,
				     &scanner->param,
				     scanbuf_filepos + (p-scanbuf)))) {
	    off_t filepos;

	    p -= scanner->offset;
	    filepos = scanbuf_filepos + (p-scanbuf);

	    array_foreach(&skip_ranges, range) {
		if (range->begin >= 0
			&& filepos > range->begin
			&& filepos < range->end) {
		    goto keep_scanning;
		}
	    }

	    array_foreach(&scanner->recipes, r) {
		struct operation *op;
		int ops_matched = 1;

		array_foreach(&r->ops, op) {
		    if (!op->func(p + op->offset, &op->param)) {
			ops_matched = 0;
			break;
		    }
		}

		if (ops_matched) {
		    off_t output_size;

		    fprintf(stderr, "Found %s at 0x%llX\n",
			    r->name, (long long)filepos);

		    output_size = extract(fd, r, filepos);
		    filepos_changed = 1;

		    if (output_size < 0)
			return -1;

		    if (output_size > 0 && !r->allow_overlap) {
			struct range *tmp;
			range = NULL;
			
			/* find an available range, or add one */
			array_foreach(&skip_ranges, tmp) {
			    if (tmp->begin < 0) {
				range = tmp;
			    }
			}
			if (!range)
			    range = array_add(&skip_ranges, NULL);

			range->begin = filepos;
			range->end   = filepos + output_size;

			break; /* try no more recipes */
		    }
		}
	    }

keep_scanning:
	    p += scanner->offset + 1;
	}
    }

    return filepos_changed;
}

/* Opens and scans an entire device for matches with the selected scanners. */
static void scan_disk(const char *device, const char *offset)
{
    ssize_t got;
    int fd = 0;
    int result, firsttime = 1;

    unsigned char *readbuf = buf, *scanbuf = buf;
    size_t readsize = bufsize;
    off_t offset_before_read;
    
    if (strcmp(device, "-") != 0 &&
	    (fd = open(device, O_RDONLY)) == -1) {
	fprintf(stderr, "opening %s: %s\n", device, strerror(errno));
	return;
    }

    offset_before_read = rich_seek(fd, offset);
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

    /* Reset all ranges after processing a file */
    {
	struct range *range;
	array_foreach(&skip_ranges, range) {
	    range->begin = -1;
	}
    }
}

/* Parse a recipe file, adding it to the global list of scanners.  If
 * global_scanner is not NULL, it points to a scanner that will be forced into
 * this recipe.
 * Returns 0 on success, -1 on fatal error. */
static int parse_recipe(const char *recipefile, 
	struct scanner *global_scanner)
{
    struct scanner *scanner = global_scanner;
    struct recipe r;
    FILE *fh = NULL; 
    char **sp, *search_path[] = { "recipes", NULL, NULL };

#ifdef RECIPE_PATH
    search_path[1] = RECIPE_PATH;
#endif

    fh = fopen(recipefile, "r");
    for (sp = search_path; !fh && *sp; sp++) {
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", *sp, recipefile);
	fh = fopen(path, "r");
    }
    
    if (!fh) {
	fprintf(stderr, "Opening %s: %s\n", recipefile, strerror(errno));
	return -1;
    }

    recipe_init(&r);

    {
	const char *basename = strrchr(recipefile, '/');
	r.name = strdup(basename ? basename + 1 : recipefile);
    }

    while (fgets(buf, bufsize, fh)) {
	char opname[64];
	int magic_offset, param_offset;
	buf[strlen(buf) - 1] = '\0'; /* kill trailing newline */

	if (sscanf(buf, "%d %63s %n",
		    &magic_offset, opname, &param_offset) >= 2) {
	    ssize_t len = 0;
	    char *param = buf + param_offset;
	    
	    if (!scanner) {
		/* Try to make it the scanner function */
		
		if (strcmp(opname, "string") == 0
			|| strcmp(opname, "char") == 0) {
		    struct string string;
		    string_init(&string, buf + param_offset);

		    scanner = scanner_new();
		    scanner->offset = magic_offset;

		    if (string.l == 1) {
			scanner->func = scanner_char;
			scanner->param.c = string.s[0];
			string_destroy(&string);
			len = 1;
		    } else if (string.l > 1) {
			scanner->func = scanner_string;
			scanner->param.scanstring.string = string;
			scanner_string_init(&scanner->param);
			len = string.l;
		    }
		}

		if (scanner) {
		    scanner->extra_len = len - 1;
		}
	    }

	    if (len <= 0) {
		/* If it did not become a scanner function it will become an
		 * operation */
		struct operation *op = array_add(&r.ops, NULL);

		op->offset = magic_offset;

		if (strcmp(opname, "string") == 0
			|| strcmp(opname, "char") == 0) {
		    op->func = op_string;
		    string_init(&op->param.string, param);
		    len = op->param.string.l;

		} else if (strcmp(opname, "int32") == 0) {
		    char buf_val[16], buf_mask[16];
		    op->func = op_int32;
		    if (sscanf(param, "%15s %15s", buf_val, buf_mask) != 2) {
			fprintf(stderr, "%s: invalid parameter for %s: %s\n",
				recipefile, opname, param);
			fclose(fh);
			return -1;
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
		     - Add another "else if" clause here.  It must assign to
		       op->func and to len.
		     - The value assigned to len should be the maximum number of
		       bytes your test function might read.  Never read more
		       bytes than this.
		     - Add a case to op_destroy of cleanup is needed
		     The test functions you add should be general and reusable,
		     such as string, int, md5, regexp.  To make them specific
		     they should take a parameter.
		*/

	    }

	    if (len <= 0) {
		fprintf(stderr, "Invalid operation '%s'\n", opname);
		fclose(fh);
		return -1;
	    }
	    len += magic_offset;
	    if (overlap < len)
		overlap = len;

	} else if (buf[0] != '#' && buf[0] != '\0') {

	    if (sscanf(buf, "extension %63s", r.extension) == 1) {
		/* do nothing */

	    } else if (strncmp(buf, "command ", 8) == 0) {
		r.command = strdup(buf + 8);

	    } else if (strncmp(buf, "postextract ", 12) == 0) {
		r.postextract = strdup(buf + 12);

	    } else if (strncmp(buf, "rename ", 7) == 0) {
		r.rename = strdup(buf + 7);

	    } else if (sscanf(buf, "min_output_file %ld", &r.min_output_file)
		    == 1) {
		/* do nothing */
	    } else if (strcmp(buf, "allow_overlap") == 0) {
		r.allow_overlap = 1;
	    } else {
		fprintf(stderr, "Invalid line in %s: %s\n", recipefile, buf);
		fclose(fh);
		return -1;
	    }

	}
    }

    if (ferror(fh)) {
	fclose(fh);
	fprintf(stderr, "fgets(): %s: %s\n", recipefile, strerror(errno));
	return -1;
    }

    fclose(fh);

    if (!scanner || !r.command) {
	fprintf(stderr, 
"Invalid recipe file %s.\n"
"It must contain one scanner directive, one command directive and at least\n"
"one match directive\n", recipefile);
	return -1;
    }

    scanner = scanner_add_or_reuse(scanner);
    array_add(&scanner->recipes, &r);

    /* Round overlap to machine word boundary, for faster memcpy */
    overlap +=   sizeof(long) - 1;
    overlap &= ~(sizeof(long) - 1);

    return 0;
}

static void usage(void)
{
    printf(
"Usage: magicrescue [-I FILE] [-M MODE] [-O [+-=][0x]OFFSET] [-b BLOCKSIZE]\n"
"\t-d OUTPUT_DIR -r RECIPE1 [-r RECIPE2 [...]] DEVICE1 [DEVICE2 [...]]\n"
"\n"
"  -b  Only consider files starting at a multiple of BLOCKSIZE.\n"
"  -d  Mandatory.  Output directory for found files.\n"
"  -r  Mandatory.  Recipe name, file or directory.\n"
"  -I  Read input file names from this file (\"-\" for stdin)\n"
"  -M  Produce machine-readable output to stdout.\n"
"  -O  Resume from specified offset (hex or decimal) in the first device.\n"
"\n");
}

int main(int argc, char **argv)
{
    int c;
    struct scanner *global_scanner = NULL;
    char *start_offset = NULL;
    char *file_names_from = NULL;

    array_init(&scanners,    sizeof(struct scanner));
    array_init(&skip_ranges, sizeof(struct range));

    /* Some recipes depend on parsing error messages from various programs */
    putenv("LC_ALL=C");

#ifdef HAVE_GETRLIMIT
    /* Some helper programs will segfault or exhaust memory on malformed
     * input.  This should prevent core files and swap storms */
    {
	struct rlimit rl;
	rl.rlim_cur = rl.rlim_max = 0;
	setrlimit(RLIMIT_CORE, &rl);

	if (getrlimit(RLIMIT_AS, &rl) == 0 && (
		    rl.rlim_max == RLIM_INFINITY ||
		    rl.rlim_max > MAX_MEMORY)) {
	    rl.rlim_cur = rl.rlim_max = MAX_MEMORY;

	    setrlimit(RLIMIT_AS, &rl);
	}
    }
#endif /* HAVE_GETRLIMIT */

    progress.position = 0;
    progress.device[0] = progress.device_basename[0] = '\0';

    buf = malloc(bufsize);

    sprintf(buf, "PATH=%s:%s", getenv("PATH"), "tools");
#ifdef COMMAND_PATH
    sprintf(buf + strlen(buf), ":%s", COMMAND_PATH);
#endif
    putenv(strdup(buf));

    while ((c = getopt(argc, argv, "b:d:r:I:M:O:")) >= 0) {
	switch (c) {
	case 'b': {
	    long boundary = atol(optarg);
	    if (global_scanner)
		scanner_destroy(global_scanner);

	    if (boundary <= 1) {
		global_scanner = NULL;
	    } else {
		if (boundary & (boundary - 1)) {
		    fprintf(stderr,
			    "Error: block size must be a multiple of two\n");
		    return 1;
		}
		global_scanner = scanner_new();
		global_scanner->func = scanner_block;
		global_scanner->param.block = boundary;
	    }
	}
	break; case 'd': {
	    struct stat dirstat;
	    if (stat(optarg, &dirstat) == 0 && dirstat.st_mode & S_IFDIR) {
		output_dir = optarg;
	    } else {
		fprintf(stderr, "Invalid directory %s\n", optarg);
		return 1;
	    }
	}
	break; case 'r': {
	    struct stat st;
	    DIR *dh;
	    if (stat(optarg, &st) == 0 && S_ISDIR(st.st_mode)
		    && (dh = opendir(optarg))) {
		struct dirent *dirent;
		char fullname[PATH_MAX];

		while ((dirent = readdir(dh))) {
		    if (dirent->d_name[0] != '.') {
			snprintf(fullname, PATH_MAX, "%s/%s",
				optarg, dirent->d_name);
			if (parse_recipe(fullname, global_scanner) != 0) {
			    return 1;
			}
		    }
		}
		closedir(dh);

	    } else if (parse_recipe(optarg, global_scanner) != 0) {
		return 1;
	    }
	}
	break; case 'I':
	    file_names_from = optarg;

	break; case 'M':
	    if (strchr(optarg, 'i'))
		machine_output |= OUT_I;
	    if (strchr(optarg, 'o'))
		machine_output |= OUT_O;
	    setvbuf(stdout, NULL, _IOLBF, 0);

	break; case 'O':
	    start_offset = optarg;

	break; default:
	    fprintf(stderr, "Error parsing options.\n");
	    usage();
	    return 1;
	}
    }

    if (array_count(&scanners) == 0 || !output_dir) {
	usage();
	return 1;
    }

    name_mode = argc-optind > 5 || file_names_from ? MODE_FILES : MODE_DEVICE;

    signal(SIGINT,  signal_beforedeath);
    signal(SIGTERM, signal_beforedeath);
    signal(SIGSEGV, signal_beforedeath);
    signal(SIGPIPE, signal_beforedeath);
    
    while (optind < argc) {
	scan_disk(argv[optind++], start_offset);
	start_offset = NULL;
    }

    if (file_names_from) {
	char inputfile[PATH_MAX];
	FILE *fh = strcmp(file_names_from, "-") == 0 ? stdin : 
		fopen(file_names_from, "r");
	
	if (!fh) {
	    fprintf(stderr, "Opening %s: %s\n",
		    file_names_from, strerror(errno));
	    return 1;
	}

	while (fgets(inputfile, sizeof inputfile, fh)) {
	    inputfile[strlen(inputfile)-1] = '\0'; /* kill newline */
	    scan_disk(inputfile, NULL);
	}
	fclose(fh);
    }

    scanners_free();
    array_destroy(&skip_ranges);
    free(buf);
    return 0;
}

/* vim: ts=8 sw=4 noet tw=80
 */
