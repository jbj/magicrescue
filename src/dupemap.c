/*
 * dupemap, part of Magic Rescue
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

#ifdef HAVE_NDBM

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "find_dbm.h"
#include "recur.h"
#include "util.h"

enum operation { SCAN=1, REPORT=2, DELETE=4, HASDB=8 };

enum mode { MODE_READDB=1, MODE_WRITEDB=2, MODE_DELETE=4, MODE_REPORT=8,
            MODE_TEMPDB=16 };

/*
 *
 * DATABASE CODE
 *
 * An implementation of the database layer should provide a dbhandle typedef
 * and the csdb_{open,open_tempdb,exists,store,close} functions.  No
 * values are stored in the database, only the existence of keys matters.
 *
 */
typedef struct {
    DBM *db;
    char *tempdir;
} dbhandle;

static int csdb_open_tempdb(dbhandle *db)
{
    int i = 0, rv;
    char *env_tmp = getenv("TMP");
    if (!env_tmp || !*env_tmp)
	env_tmp = "/tmp";

    db->tempdir = malloc(PATH_MAX);
    do {
	snprintf(db->tempdir, PATH_MAX, "%s/dupemap_%d", env_tmp, i++);
	rv = mkdir(db->tempdir, 0700);
    } while (rv != 0 && errno == EEXIST);

    if (rv != 0) {
	perror("making temporary directory");
	free(db->tempdir);
	return -1;

    } else {
	char file[PATH_MAX];
	snprintf(file, PATH_MAX, "%s/tempdb", db->tempdir);
	db->db = dbm_open(file, O_RDWR | O_CREAT, 0666);

	if (!db->db) {
	    perror("dbm_open");
	    free(db->tempdir);
	    return -1;
	}
    }

    return 0;
}

static int csdb_open(dbhandle *db, char *file, enum mode mode)
{
    int openmode = mode & MODE_READDB ? O_RDONLY : O_RDWR | O_CREAT;

    db->tempdir = NULL;
    db->db = dbm_open(file, openmode, 0666);
    if (!db->db) {
        perror("dbm_open");
	return -1;
    }
    return 0;
}

static int csdb_exists(dbhandle *db, void *key, int len)
{
    datum d, value;
    d.dptr = key;
    d.dsize = len;

    value = dbm_fetch(db->db, d);
    return value.dptr != NULL;
}

static int csdb_store(dbhandle *db, void *key, int len)
{
    datum d, value;

    d.dptr = key;
    d.dsize = len;
    value.dptr = "";
    value.dsize = DBM_KEY_LEN;

    dbm_store(db->db, d, value, DBM_INSERT);
    return 0;
}

static int csdb_close(dbhandle *db)
{
    dbm_close(db->db);

    if (db->tempdir) {
	rm_rf(db->tempdir);
	free(db->tempdir);
    }
    return 0;
}

/*
 * 
 * CHECKSUM CODE
 *
 * An implementation should provide the checksum_t typedef and the
 * checksum_calc function.
 *
 */

/* This CRC32 implementation is derived from Weeder
 * <http://members.teleweb.at/erikajo/weeder.htm>
 */
static const unsigned long crctab[256] = {
0x0,
0x04C11DB7,0x09823B6E,0x0D4326D9,0x130476DC,0x17C56B6B,0x1A864DB2,0x1E475005,
0x2608EDB8,0x22C9F00F,0x2F8AD6D6,0x2B4BCB61,0x350C9B64,0x31CD86D3,0x3C8EA00A,
0x384FBDBD,0x4C11DB70,0x48D0C6C7,0x4593E01E,0x4152FDA9,0x5F15ADAC,0x5BD4B01B,
0x569796C2,0x52568B75,0x6A1936C8,0x6ED82B7F,0x639B0DA6,0x675A1011,0x791D4014,
0x7DDC5DA3,0x709F7B7A,0x745E66CD,0x9823B6E0,0x9CE2AB57,0x91A18D8E,0x95609039,
0x8B27C03C,0x8FE6DD8B,0x82A5FB52,0x8664E6E5,0xBE2B5B58,0xBAEA46EF,0xB7A96036,
0xB3687D81,0xAD2F2D84,0xA9EE3033,0xA4AD16EA,0xA06C0B5D,0xD4326D90,0xD0F37027,
0xDDB056FE,0xD9714B49,0xC7361B4C,0xC3F706FB,0xCEB42022,0xCA753D95,0xF23A8028,
0xF6FB9D9F,0xFBB8BB46,0xFF79A6F1,0xE13EF6F4,0xE5FFEB43,0xE8BCCD9A,0xEC7DD02D,
0x34867077,0x30476DC0,0x3D044B19,0x39C556AE,0x278206AB,0x23431B1C,0x2E003DC5,
0x2AC12072,0x128E9DCF,0x164F8078,0x1B0CA6A1,0x1FCDBB16,0x018AEB13,0x054BF6A4,
0x0808D07D,0x0CC9CDCA,0x7897AB07,0x7C56B6B0,0x71159069,0x75D48DDE,0x6B93DDDB,
0x6F52C06C,0x6211E6B5,0x66D0FB02,0x5E9F46BF,0x5A5E5B08,0x571D7DD1,0x53DC6066,
0x4D9B3063,0x495A2DD4,0x44190B0D,0x40D816BA,0xACA5C697,0xA864DB20,0xA527FDF9,
0xA1E6E04E,0xBFA1B04B,0xBB60ADFC,0xB6238B25,0xB2E29692,0x8AAD2B2F,0x8E6C3698,
0x832F1041,0x87EE0DF6,0x99A95DF3,0x9D684044,0x902B669D,0x94EA7B2A,0xE0B41DE7,
0xE4750050,0xE9362689,0xEDF73B3E,0xF3B06B3B,0xF771768C,0xFA325055,0xFEF34DE2,
0xC6BCF05F,0xC27DEDE8,0xCF3ECB31,0xCBFFD686,0xD5B88683,0xD1799B34,0xDC3ABDED,
0xD8FBA05A,0x690CE0EE,0x6DCDFD59,0x608EDB80,0x644FC637,0x7A089632,0x7EC98B85,
0x738AAD5C,0x774BB0EB,0x4F040D56,0x4BC510E1,0x46863638,0x42472B8F,0x5C007B8A,
0x58C1663D,0x558240E4,0x51435D53,0x251D3B9E,0x21DC2629,0x2C9F00F0,0x285E1D47,
0x36194D42,0x32D850F5,0x3F9B762C,0x3B5A6B9B,0x0315D626,0x07D4CB91,0x0A97ED48,
0x0E56F0FF,0x1011A0FA,0x14D0BD4D,0x19939B94,0x1D528623,0xF12F560E,0xF5EE4BB9,
0xF8AD6D60,0xFC6C70D7,0xE22B20D2,0xE6EA3D65,0xEBA91BBC,0xEF68060B,0xD727BBB6,
0xD3E6A601,0xDEA580D8,0xDA649D6F,0xC423CD6A,0xC0E2D0DD,0xCDA1F604,0xC960EBB3,
0xBD3E8D7E,0xB9FF90C9,0xB4BCB610,0xB07DABA7,0xAE3AFBA2,0xAAFBE615,0xA7B8C0CC,
0xA379DD7B,0x9B3660C6,0x9FF77D71,0x92B45BA8,0x9675461F,0x8832161A,0x8CF30BAD,
0x81B02D74,0x857130C3,0x5D8A9099,0x594B8D2E,0x5408ABF7,0x50C9B640,0x4E8EE645,
0x4A4FFBF2,0x470CDD2B,0x43CDC09C,0x7B827D21,0x7F436096,0x7200464F,0x76C15BF8,
0x68860BFD,0x6C47164A,0x61043093,0x65C52D24,0x119B4BE9,0x155A565E,0x18197087,
0x1CD86D30,0x029F3D35,0x065E2082,0x0B1D065B,0x0FDC1BEC,0x3793A651,0x3352BBE6,
0x3E119D3F,0x3AD08088,0x2497D08D,0x2056CD3A,0x2D15EBE3,0x29D4F654,0xC5A92679,
0xC1683BCE,0xCC2B1D17,0xC8EA00A0,0xD6AD50A5,0xD26C4D12,0xDF2F6BCB,0xDBEE767C,
0xE3A1CBC1,0xE760D676,0xEA23F0AF,0xEEE2ED18,0xF0A5BD1D,0xF464A0AA,0xF9278673,
0xFDE69BC4,0x89B8FD09,0x8D79E0BE,0x803AC667,0x84FBDBD0,0x9ABC8BD5,0x9E7D9662,
0x933EB0BB,0x97FFAD0C,0xAFB010B1,0xAB710D06,0xA6322BDF,0xA2F33668,0xBCB4666D,
0xB8757BDA,0xB5365D03,0xB1F740B4
};

typedef struct {
    long crc;
    off_t filesize;
} checksum_t;

#define BUFLEN ( 1 << 16 )

static int checksum_calc(checksum_t *checksum, const char *file)
{
    static char buf[BUFLEN];
    ssize_t bytes_read;
    off_t _length = 0;
    long _crc = 0;
    FILE *fp;

    memset(checksum, 0, sizeof checksum);

    fp = fopen (file, "rb");
    if (fp == NULL) {
	return -1;
    }

    while ((bytes_read = fread(buf, 1, BUFLEN, fp)) > 0) {
	char *cp = buf;

	_length += bytes_read;
	while (bytes_read--)
	    _crc = (_crc << 8) ^ crctab[((_crc >> 24) ^ *(cp++)) & 0xFF];
    }

    if (ferror(fp)) {
	fclose(fp);
	return -1;
    }
    fclose(fp);

    checksum->filesize = _length;

    for (; _length; _length >>=8 )
	_crc = (_crc << 8) ^ crctab[((_crc >> 24) ^ _length) & 0xFF];

    checksum->crc = ~_crc & 0xFFFFFFFF;

    return 0;
}

/*
 *
 * MAIN PROGRAM
 *
 */
static dbhandle *global_dbhandle = NULL;
static off_t min_size = 1, max_size = 0;
static enum mode mode;

static void signal_beforedeath(int signo)
{
    fprintf(stderr, "\ndupemap: killed by signal %d\n", signo);
    csdb_close(global_dbhandle);

    exit(0);
}

static enum mode parse_mode(char *str, int hasdb) {
    char *tok;
    enum operation op = 0;
    unsigned int i;

    struct { char *opname; int opval; } opnames[] = {
	{ "scan",   SCAN    },
	{ "report", REPORT  },
	{ "delete", DELETE  },
    };
    struct { enum operation op; enum mode mode; } op2mode[] = {
	{ HASDB | SCAN,            MODE_WRITEDB },
	{ HASDB | SCAN   | REPORT, MODE_WRITEDB | MODE_REPORT },

	{ HASDB | REPORT,          MODE_READDB | MODE_REPORT },
	{ HASDB | DELETE,          MODE_READDB | MODE_DELETE },
	{ HASDB | DELETE | REPORT, MODE_READDB | MODE_DELETE | MODE_REPORT },

	{ REPORT                , MODE_TEMPDB | MODE_REPORT },
	{ DELETE                , MODE_TEMPDB | MODE_DELETE },
	{ DELETE | REPORT       , MODE_TEMPDB | MODE_DELETE | MODE_REPORT },
	{ SCAN | REPORT         , MODE_TEMPDB | MODE_REPORT },
	{ SCAN | DELETE         , MODE_TEMPDB | MODE_DELETE },
	{ SCAN | DELETE | REPORT, MODE_TEMPDB | MODE_DELETE | MODE_REPORT },
    };

    for (tok = strtok(str, ", "); tok; tok = strtok(NULL, ", ")) {
	for (i = 0; i < sizeof(opnames)/sizeof(*opnames); i++) {
	    if (strcmp(tok, opnames[i].opname) == 0)
		op |= opnames[i].opval;
	}
    }
    if (hasdb) op |= HASDB;
    
    for (i = 0; i < sizeof(op2mode)/sizeof(*op2mode); i++) {
	if (op2mode[i].op == op)
	    return op2mode[i].mode;
    }
    return 0;
}

static void scan_file(const char *name, const struct stat *st,
	dbhandle *db) {
    checksum_t checksum;

    if (!(st->st_mode & S_IFREG
		&& st->st_size >= min_size
		&& (max_size > 0 ? st->st_size <= max_size : 1)
		&& checksum_calc(&checksum, name) == 0 ))
	return;

    if (mode & MODE_TEMPDB) {

	if (csdb_exists(db, &checksum, sizeof checksum)) {

	    if (mode & MODE_REPORT)
		printf("%s\n", name);
	    if (mode & MODE_DELETE)
		unlink(name);

	} else {
	    csdb_store(db, &checksum, sizeof checksum);
	}

    } else if (mode & MODE_WRITEDB) {

	if (mode & MODE_REPORT)
	    printf("%s\n", name);

	csdb_store(db, &checksum, sizeof checksum);

    } else if (mode & MODE_READDB
	    && csdb_exists(db, &checksum, sizeof checksum)) {

	if (mode & MODE_REPORT)
	    printf("%s\n", name);

	if (mode & MODE_DELETE)
	    unlink(name);

    }
}

static void usage(void)
{
    printf(
"Usage: dupemap [OPTIONS] OPERATION PATH...\n"
"Where OPERATION is one of the operations listed in the manpage.\n"
"\n"
"Options:\n"
"  -d DATABASE   Read/write from a database on disk\n"
"  -I FILE       Read input file names from this file (\"-\" for stdin)\n"
"  -m MINSIZE    Exclude files below this size\n"
"  -M MAXSIZE    Exclude files above this size\n"
);
}

int main(int argc, char **argv)
{
    struct recur *recur;
    int c;
    char name[PATH_MAX], *dbname = NULL;
    char *file_names_from = NULL;
    struct stat st;
    dbhandle db;

    while ((c = getopt(argc, argv, "d:m:I:M:")) >= 0) {
	switch (c) {
	case 'd':
	    dbname = optarg;

	break; case 'I':
	    file_names_from = optarg;
	    
	break; case 'm':
#ifdef HAVE_ATOLL
	    min_size = (off_t)atoll(optarg);
#else
	    min_size = (off_t)atol(optarg);
#endif
	    if (min_size <= 0)
		min_size = 1;

	break; case 'M':
#ifdef HAVE_ATOLL
	    max_size = (off_t)atoll(optarg);
#else
	    max_size = (off_t)atol(optarg);
#endif

	break; default:
	    fprintf(stderr, "Error parsing options.\n");
	    usage();
	    return 1;
	}
    }

    argv += optind;
    argc -= optind;

    if (argc < 1) {
	usage();
	return 1;
    }

    mode = parse_mode(argv[0], dbname != NULL);
    if (!mode) {
	fprintf(stderr, "Invalid operation\n");
	return 1;
    }

    if (dbname) {
	if (csdb_open(&db, dbname, mode) != 0) {
	    fprintf(stderr, "Cannot open database\n");
	    return 1;
	}
    } else {
	if (csdb_open_tempdb(&db) != 0) {
	    fprintf(stderr, "Cannot open temporary database\n");
	    return 1;
	}
    }

    global_dbhandle = &db;

    signal(SIGINT,  signal_beforedeath);
    signal(SIGTERM, signal_beforedeath);
    signal(SIGSEGV, signal_beforedeath);
    signal(SIGPIPE, signal_beforedeath);
    
    if (argc > 1) {
	recur = recur_open(argv + 1);
	if (!recur) {
	    csdb_close(&db);
	    return 0;
	}

	while (recur_next(recur, name, &st) == 0) {
	    scan_file(name, &st, &db);
	}
	recur_close(recur);
    }

    if (file_names_from) {
	FILE *fh = strcmp(file_names_from, "-") == 0 ? stdin : 
		fopen(file_names_from, "r");
	
	if (!fh) {
	    fprintf(stderr, "Opening %s: %s\n",
		    file_names_from, strerror(errno));
	    csdb_close(&db);
	    return 1;
	}

	while (fgets(name, sizeof name, fh)) {
	    name[strlen(name)-1] = '\0'; /* kill newline */
	    if (stat(name, &st) == 0)
		scan_file(name, &st, &db);
	}
	fclose(fh);
    }

    csdb_close(&db);
    global_dbhandle = NULL;
    
    return 0;
}

#else /* not HAVE_DBM */
# include <stdio.h>

int main()
{
    printf(
"dupemap was not compiled because no ndbm.h was found on your system.  Please\n"
"install the development packages for Berkeley DB or GDBM and recompile.\n"
	  );
    return 0;
}
#endif /* not HAVE_DBM */
