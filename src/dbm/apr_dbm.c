/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 */

#include "apr.h"
#include "apr_errno.h"
#include "apr_pools.h"

#include "apu_private.h"
#include "apr_dbm.h"

#if APU_USE_SDBM
#include "sdbm.h"

typedef SDBM *real_file_t;
typedef sdbm_datum real_datum_t;

#define APR_DBM_CLOSE(f)	sdbm_close(f)
#define APR_DBM_FETCH(f, k)	sdbm_fetch((f), (k))
#define APR_DBM_STORE(f, k, v)	sdbm_store((f), (k), (v), SDBM_REPLACE)
#define APR_DBM_DELETE(f, k)	sdbm_delete((f), (k))
#define APR_DBM_FIRSTKEY(f)	sdbm_firstkey(f)
#define APR_DBM_NEXTKEY(f, k)	sdbm_nextkey(f)
#define APR_DBM_FREEDATUM(f, d)	if (0) ; else	/* stop "no effect" warning */

#define APR_DBM_DBMODE_RO       APR_READ
#define APR_DBM_DBMODE_RW       (APR_READ | APR_WRITE)
#define APR_DBM_DBMODE_RWCREATE (APR_READ | APR_WRITE | APR_CREATE)

#elif APU_USE_GDBM
#include <gdbm.h>
#include <stdlib.h>     /* for free() */

typedef GDBM_FILE real_file_t;
typedef datum real_datum_t;

#define APR_DBM_CLOSE(f)	gdbm_close(f)
#define APR_DBM_FETCH(f, k)	gdbm_fetch((f), (k))
#define APR_DBM_STORE(f, k, v)	g2s(gdbm_store((f), (k), (v), GDBM_REPLACE))
#define APR_DBM_DELETE(f, k)	g2s(gdbm_delete((f), (k)))
#define APR_DBM_FIRSTKEY(f)	gdbm_firstkey(f)
#define APR_DBM_NEXTKEY(f, k)	gdbm_nextkey((f), (k))
#define APR_DBM_FREEDATUM(f, d)	((d).dptr ? free((d).dptr) : 0)

#define APR_DBM_DBMODE_RO       GDBM_READER
#define APR_DBM_DBMODE_RW       GDBM_WRITER
#define APR_DBM_DBMODE_RWCREATE GDBM_WRCREAT

/* map a GDBM error to an apr_status_t */
static apr_status_t g2s(int gerr)
{
    if (gerr == -1) {
        /* ### need to fix this */
        return APR_EINVAL;
    }

    return APR_SUCCESS;
}

#else
#error a DBM implementation was not specified
#endif


struct apr_dbm_t
{
    apr_pool_t *pool;
    real_file_t file;

    int errcode;
    const char *errmsg;
};

/* apr_datum <=> real_datum casting/conversions */
#define A2R_DATUM(d)    (*(real_datum_t *)&(d))
#define R2A_DATUM(d)    (*(apr_datum_t *)&(d))


static apr_status_t set_error(apr_dbm_t *db)
{
    apr_status_t rv = APR_SUCCESS;

#if APU_USE_SDBM

    if ((db->errcode = sdbm_error(db->file)) == 0) {
        db->errmsg = NULL;
    }
    else {
        db->errmsg = "I/O error occurred.";
        rv = APR_EINVAL;        /* ### need something better */
    }

    /* captured it. clear it now. */
    sdbm_clearerr(db->file);

#elif APU_USE_GDBM

    if ((db->errcode = gdbm_errno) == GDBM_NO_ERROR) {
        db->errmsg = NULL;
    }
    else {
        db->errmsg = gdbm_strerror(gdbm_errno);
        rv = APR_EINVAL;        /* ### need something better */
    }

    /* captured it. clear it now. */
    gdbm_errno = GDBM_NO_ERROR;

#endif

    return rv;
}

apr_status_t apr_dbm_open(const char *pathname, apr_pool_t *pool, int mode,
                          apr_dbm_t **pdb)
{
    real_file_t file;
    int dbmode;

    *pdb = NULL;

    switch (mode) {
    case APR_DBM_READONLY:
        dbmode = APR_DBM_DBMODE_RO;
        break;
    case APR_DBM_READWRITE:
        dbmode = APR_DBM_DBMODE_RW;
        break;
    case APR_DBM_RWCREATE:
        dbmode = APR_DBM_DBMODE_RWCREATE;
        break;
    default:
        return APR_EINVAL;
    }

#if APU_USE_SDBM
    {
        apr_status_t rv;

        rv = sdbm_open(&file, pathname, dbmode, APR_OS_DEFAULT, pool);
        if (rv != APR_SUCCESS)
            return rv;
    }
#elif APU_USE_GDBM
    {
        /* Note: stupid cast to get rid of "const" on the pathname */
        file = gdbm_open((char *) pathname, 0, dbmode, 0660, NULL);
        if (file == NULL)
            return APR_EINVAL;      /* ### need a better error */
    }
#endif

    /* we have an open database... return it */
    *pdb = apr_pcalloc(pool, sizeof(**pdb));
    (*pdb)->pool = pool;
    (*pdb)->file = file;

    return APR_SUCCESS;
}

void apr_dbm_close(apr_dbm_t *db)
{
    APR_DBM_CLOSE(db->file);
}

apr_status_t apr_dbm_fetch(apr_dbm_t *db, apr_datum_t key, apr_datum_t *pvalue)
{
    *(real_datum_t *) pvalue = APR_DBM_FETCH(db->file, A2R_DATUM(key));

    /* store the error info into DB, and return a status code. Also, note
       that *pvalue should have been cleared on error. */
    return set_error(db);
}

apr_status_t apr_dbm_store(apr_dbm_t *db, apr_datum_t key, apr_datum_t value)
{
    apr_status_t rv;

    rv = APR_DBM_STORE(db->file, A2R_DATUM(key), A2R_DATUM(value));

    /* ### is this the right handling of set_error() and rv? */

    /* store the error info into DB, and return a status code. Also, note
       that *pvalue should have been cleared on error. */
    (void) set_error(db);

    return rv;
}

apr_status_t apr_dbm_delete(apr_dbm_t *db, apr_datum_t key)
{
    apr_status_t rv;

    rv = APR_DBM_DELETE(db->file, A2R_DATUM(key));

    /* ### is this the right handling of set_error() and rv? */

    /* store the error info into DB, and return a status code. Also, note
       that *pvalue should have been cleared on error. */
    (void) set_error(db);

    return rv;
}

int apr_dbm_exists(apr_dbm_t *db, apr_datum_t key)
{
    int exists;

#if APU_USE_SDBM
    {
	sdbm_datum value = sdbm_fetch(db->file, A2R_DATUM(key));
	sdbm_clearerr(db->file);	/* don't need the error */
	exists = value.dptr != NULL;
    }
#elif APU_USE_GDBM
    exists = gdbm_exists(db->file, A2R_DATUM(key)) != 0;
#endif
    return exists;
}

apr_status_t apr_dbm_firstkey(apr_dbm_t *db, apr_datum_t *pkey)
{
    *(real_datum_t *) pkey = APR_DBM_FIRSTKEY(db->file);

    /* store the error info into DB, and return a status code. Also, note
       that *pvalue should have been cleared on error. */
    return set_error(db);
}

apr_status_t apr_dbm_nextkey(apr_dbm_t *db, apr_datum_t *pkey)
{
    *(real_datum_t *) pkey = APR_DBM_NEXTKEY(db->file, A2R_DATUM(*pkey));

    /* store the error info into DB, and return a status code. Also, note
       that *pvalue should have been cleared on error. */
    return set_error(db);
}

void apr_dbm_freedatum(apr_dbm_t *db, apr_datum_t data)
{
    APR_DBM_FREEDATUM(db, data);
}

void apr_dbm_geterror(apr_dbm_t *db, int *errcode, const char **errmsg)
{
    *errcode = db->errcode;
    *errmsg = db->errmsg;
}
