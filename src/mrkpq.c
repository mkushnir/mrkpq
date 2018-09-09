#include <assert.h>
#include <stdlib.h>

//#define TRRET_DEBUG
#include <mrkcommon/dumpm.h>
#include <mrkcommon/util.h>
#include <mrkthr.h>

#include <mrkpq.h>

#include "diag.h"


static PGconn *
mrkpq_postconnect(PGconn *conn)
{
    int res;
    res = 0;

    if (PQstatus(conn) == CONNECTION_BAD) {
        CTRACE("PQ error: %s", PQerrorMessage(conn));
        res = MRKPQ_POSTCONNECT + 1;
        goto err;
    }

    if (PQsetnonblocking(conn, 1) != 0) {
        CTRACE("PQ error: %s", PQerrorMessage(conn));
        res = MRKPQ_POSTCONNECT + 2;
        goto err;
    }

    while (true) {
        int fd;
        PostgresPollingStatusType pst;

        fd = PQsocket(conn);
        pst = PQconnectPoll(conn);

        switch (pst) {
        case PGRES_POLLING_READING:
            if ((res = mrkthr_wait_for_read(fd)) != 0) {
                CTRACE("res=%d", res);
                res = MRKPQ_POSTCONNECT + 3;
                goto err;
            }
            break;

        case PGRES_POLLING_WRITING:
            if ((res = mrkthr_wait_for_write(fd)) != 0) {
                CTRACE("res=%d", res);
                res = MRKPQ_POSTCONNECT + 4;
                goto err;
            }
            break;

        case PGRES_POLLING_FAILED:
            CTRACE("PQconnectPoll error: %s", PQerrorMessage(conn));
            res = MRKPQ_POSTCONNECT + 5;
            goto err;

        case PGRES_POLLING_OK:
            break;

        default:
            res = MRKPQ_POSTCONNECT + 6;
            goto err;
        }

        if (!(pst == PGRES_POLLING_READING || pst == PGRES_POLLING_WRITING)) {
            break;
        }
    }

end:
    return conn;

err:
    TR(res);
    PQfinish(conn);
    conn = NULL;
    goto end;
}



PGconn *
mrkpq_connect_str(const char *cstr)
{
    PGconn *conn;

    if ((conn = PQconnectStart(cstr)) == NULL) {
        TRRETNULL(MRKPQ_CONNECT_STR + 1);
    }
    return mrkpq_postconnect(conn);
}


PGconn *
mrkpq_connect_params(const char *const *keywords,
                     const char *const *values,
                     int expand_dbname)
{
    PGconn *conn;

    if ((conn = PQconnectStartParams(keywords,
                                     values,
                                     expand_dbname)) == NULL) {
        TRRETNULL(MRKPQ_CONNECT_PARAMS + 1);
    }
    return mrkpq_postconnect(conn);
}


int
mrkpq_reset(PGconn *conn)
{
    int res;

    res = 0;
    if (PQresetStart(conn) != 0) {
        res = MRKPQ_RESET + 1;
        goto err;
    }

    if (PQsetnonblocking(conn, 1) != 0) {
        CTRACE("PQ error: %s", PQerrorMessage(conn));
        goto err;
    }

    while (true) {
        int fd;
        PostgresPollingStatusType pst;

        fd = PQsocket(conn);
        pst = PQresetPoll(conn);
        switch (pst) {
        case PGRES_POLLING_READING:
            if ((res = mrkthr_wait_for_read(fd)) != 0) {
                CTRACE("res=%d", res);
                res = MRKPQ_POSTCONNECT + 1;
                goto err;
            }
            break;

        case PGRES_POLLING_WRITING:
            if ((res = mrkthr_wait_for_write(fd)) != 0) {
                CTRACE("res=%d", res);
                res = MRKPQ_POSTCONNECT + 2;
                goto err;
            }
            break;

        default:
            break;
        }

        if (!(pst == PGRES_POLLING_READING || pst == PGRES_POLLING_WRITING)) {
            break;
        }
    }

end:
    return res;

err:
    goto end;
}


static int
mrkpq_postquery(PGconn *conn,
                int flags,
                mrkpq_result_cb_t rcb,
                mrkpq_notify_cb_t ncb,
                void *udata)
{
    int res;
    int fd;

    res = 0;
    fd = PQsocket(conn);

    if (flags & MRKPQ_QUERY_SINGLE_ROW) {
        if (PQsetSingleRowMode(conn) != 1) {
            CTRACE("PQsetSingleRowMode error: %s", PQerrorMessage(conn));
        }
    }

    while ((res = PQflush(conn)) == 1) {
        int events;

        events = 0;
        if (mrkthr_wait_for_events(fd, &events) != 0) {
            res = MRKPQ_POSTQUERY + 1;
            goto err;
        }

        if (events & MRKTHR_WAIT_EVENT_READ) {
            if (PQconsumeInput(conn) == 0) {
                CTRACE("PQconsumeInput error: %s", PQerrorMessage(conn));
                res = MRKPQ_POSTQUERY + 2;
                goto err;
            }
        } else if (events & MRKTHR_WAIT_EVENT_WRITE) {
            continue;
        } else {
            break;
        }
    }

    if (res != 0) {
        CTRACE("PQflush error: %s", PQerrorMessage(conn));
        res = MRKPQ_POSTQUERY + 3;
        goto err;
    } else {
        /* seems like send queue is empty */
    }

    while (true) {
        PGresult *qres;

        while (PQisBusy(conn)) {
            if ((res = mrkthr_wait_for_read(fd)) != 0) {
                CTRACE("mrkthr_wait_for_read res=%d", res);
                res = MRKPQ_POSTQUERY + 3;
                goto err;
            }
            if (PQconsumeInput(conn) == 0) {
                CTRACE("PQconsumeInput error: %s", PQerrorMessage(conn));
                res = MRKPQ_POSTQUERY + 4;
                goto err;
            }
        }

        if ((qres = PQgetResult(conn)) == NULL) {
            break;
        }

        if (rcb(conn, qres, udata) != 0) {
            res = MRKPQ_POSTQUERY + 5;
            PQclear(qres);
            goto err;
        }

        PQclear(qres);
    }

    if (ncb != NULL) {
        while (true) {
            PGnotify *notify;

            if ((res = mrkthr_wait_for_read(fd)) != 0) {
                CTRACE("mrkthr_wait_for_read res=%d", res);
                res = MRKPQ_POSTQUERY + 6;
                goto err;
            }
            if (PQconsumeInput(conn) == 0) {
                CTRACE("PQconsumeInput error: %s", PQerrorMessage(conn));
                res = MRKPQ_POSTQUERY + 7;
                goto err;
            }

            while ((notify = PQnotifies(conn)) != NULL) {
                if (ncb(conn, notify, udata) != 0) {
                    res = MRKPQ_POSTQUERY + 8;
                    goto err;
                }
                PQfreemem(notify);
                notify = NULL;
            }
        }
    }

end:
    return res;

err:
    goto end;
}


int
mrkpq_query(PGconn *conn,
            const char *query,
            int flags,
            mrkpq_result_cb_t rcb,
            mrkpq_notify_cb_t ncb,
            void *udata)
{
    int res;

    if (PQsendQuery(conn, query) == 0) {
        CTRACE("PQsendQuery error: %s", PQerrorMessage(conn));
        res = MRKPQ_QUERY + 1;
        goto end;
    }

    res = mrkpq_postquery(conn, flags, rcb, ncb, udata);

end:
    return res;
}


int
mrkpq_query_params(PGconn *conn,
                   const char *command,
                   int nparams,
                   const Oid *param_types,
                   const char *const *param_values,
                   const int *param_lengths,
                   const int *param_formats,
                   int result_format,
                   int flags,
                   mrkpq_result_cb_t rcb,
                   mrkpq_notify_cb_t ncb,
                   void *udata)
{
    int res;

    if (PQsendQueryParams(conn,
                          command,
                          nparams,
                          param_types,
                          param_values,
                          param_lengths,
                          param_formats,
                          result_format) == 0) {
        CTRACE("PQsendQueryParams error: %s", PQerrorMessage(conn));
        res = MRKPQ_QUERY_PARAMS + 1;
        goto end;
    }

    res = mrkpq_postquery(conn, flags, rcb, ncb, udata);

end:
    return res;
}


int
mrkpq_prepare(PGconn *conn,
              const char *stmt_name,
              const char *query,
              int nparams,
              const Oid *param_types,
              int flags,
              mrkpq_result_cb_t rcb,
              mrkpq_notify_cb_t ncb,
              void *udata)
{
    int res;

    if (PQsendPrepare(conn,
                      stmt_name,
                      query,
                      nparams,
                      param_types) == 0) {
        CTRACE("PQsendPrepare error: %s", PQerrorMessage(conn));
        res = MRKPQ_PREPARE + 1;
        goto end;
    }

    res = mrkpq_postquery(conn, flags, rcb, ncb, udata);

end:
    return res;
}


int
mrkpq_query_prepared(PGconn *conn,
                     const char *stmt_name,
                     int nparams,
                     const char *const *param_values,
                     const int *param_lengths,
                     const int *param_formats,
                     int result_format,
                     int flags,
                     mrkpq_result_cb_t rcb,
                     mrkpq_notify_cb_t ncb,
                     void *udata)
{
    int res;

    if (PQsendQueryPrepared(conn,
                            stmt_name,
                            nparams,
                            param_values,
                            param_lengths,
                            param_formats,
                            result_format) == 0) {
        CTRACE("PQsendQueryPrepared error: %s", PQerrorMessage(conn));
        res = MRKPQ_QUERY_PREPARED + 1;
        goto end;
    }

    res = mrkpq_postquery(conn, flags, rcb, ncb, udata);

end:
    return res;
}
