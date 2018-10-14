#define DB_DBM_HSEARCH 1

#ifdef DBM_H_UNIX
# include <ndbm.h>
#endif

#ifdef DBM_H_DB1
# include <db1/ndbm.h>
#endif

#ifdef DBM_H_GDBM
# include <gdbm/ndbm.h>
#endif

#ifdef DBM_H_DB_H
# include <db.h>
#endif

#ifdef DBM_H_DB
# include <db/ndbm.h>
#endif
