#define DB_DBM_HSEARCH 1

#ifdef DBM_H_UNIX
# include <ndbm.h>
# define DBM_KEY_LEN 1
#endif

#ifdef DBM_H_DB1
# include <db1/ndbm.h>
# define DBM_KEY_LEN 0
#endif

#ifdef DBM_H_GDBM
# include <gdbm/ndbm.h>
# define DBM_KEY_LEN 0
#endif

#ifdef DBM_H_DB_H
# include <db.h>
# define DBM_KEY_LEN 1
#endif

#ifdef DBM_H_DB
# include <db/ndbm.h>
# define DBM_KEY_LEN 1
#endif
