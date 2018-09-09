#include <assert.h>
#include <stdbool.h>

#include "unittest.h"

#include <mrkcommon/dumpm.h>
#include <mrkcommon/util.h>
#include <mrkthr.h>

#include <mrkpq.h>

#include "diag.h"

#ifndef NDEBUG
const char *_malloc_options = "AJ";
#endif

static void
test0(void)
{
    struct {
        long rnd;
        int in;
        int expected;
    } data[] = {
        {0, 0, 0},
    };
    UNITTEST_PROLOG_RAND;

    FOREACHDATA {
        //TRACE("in=%d expected=%d", CDATA.in, CDATA.expected);
        assert(CDATA.in == CDATA.expected);
    }
}


UNUSED static void
pqconn_dump_info(PGconn *conn)
{
    CTRACE("db=%s", PQdb(conn));
    CTRACE("user=%s", PQuser(conn));
    CTRACE("pass=%s", PQpass(conn));
    CTRACE("host=%s", PQhost(conn));
    CTRACE("port=%s", PQport(conn));
    CTRACE("tty=%s", PQtty(conn));
    CTRACE("options=%s", PQoptions(conn));
    CTRACE("status=%d", PQstatus(conn));
    CTRACE("tstatus=%d", PQtransactionStatus(conn));
    //CTRACE("pstatus=%s", PQparameterStatus(conn));
    CTRACE("pversion=%d", PQprotocolVersion(conn));
    CTRACE("sversion=%d", PQserverVersion(conn));
    CTRACE("emsg=%s", PQerrorMessage(conn));
    CTRACE("socket=%d", PQsocket(conn));
    CTRACE("needpass=%d", PQconnectionNeedsPassword(conn));
    CTRACE("usedpass=%d", PQconnectionUsedPassword(conn));
    CTRACE("cenc=%d", PQclientEncoding(conn));
    CTRACE("ssl=%d", PQsslInUse(conn));
}


#define CST_STR(st)                                                            \
((st) == CONNECTION_OK ? "CONNECTION_OK" :                                     \
 (st) == CONNECTION_BAD ? "CONNECTION_BAD" :                                   \
 (st) == CONNECTION_STARTED ? "CONNECTION_STARTED" :                           \
 (st) == CONNECTION_MADE ? "CONNECTION_MADE" :                                 \
 (st) == CONNECTION_AWAITING_RESPONSE ? "CONNECTION_AWAITING_RESPONSE" :       \
 (st) == CONNECTION_AUTH_OK ? "CONNECTION_AUTH_OK" :                           \
 (st) == CONNECTION_SETENV ? "CONNECTION_SETENV" :                             \
 (st) == CONNECTION_SSL_STARTUP ? "CONNECTION_SSL_STARTUP" :                   \
 (st) == CONNECTION_NEEDED ? "CONNECTION_NEEDED" :                             \
 "<unknown>"                                                                   \
)                                                                              \


#define PST_STR(st)                                    \
((st) == PGRES_POLLING_FAILED ? "POLLING_FAILED" :     \
 (st) == PGRES_POLLING_READING ? "POLLING_READING":    \
 (st) == PGRES_POLLING_WRITING ? "POLLING_WRITING":    \
 (st) == PGRES_POLLING_OK ? "POLLING_OK":              \
 (st) == PGRES_POLLING_ACTIVE ? "POLLING_ACTIVE":      \
 "<unknown>"                                           \
)                                                      \


UNUSED static void
test_pgconn0(void)
{
    int res;
    int sock;
    PGconn *conn;
    ConnStatusType cst;
    PostgresPollingStatusType pst;

    if ((conn = PQconnectStart(
                    "postgresql://postgres@localhost/postgres")) == NULL) {
        FAIL("PQconnectStart");
    }
    if (PQsetnonblocking(conn, 1) != 0) {
        CTRACE("PQsetnonblocking error: %s", PQerrorMessage(conn));
    }
    if ((cst = PQstatus(conn)) == CONNECTION_BAD) {
        FAIL("PQstatus");
    }
    //pqconn_dump_info(conn);
    sock = PQsocket(conn);
    while (true) {
        cst = PQstatus(conn);
        pst = PQconnectPoll(conn);
        CTRACE("cst=%s pst=%s", CST_STR(cst), PST_STR(pst));
        switch (pst) {
        case PGRES_POLLING_READING:
            if ((res = mrkthr_wait_for_read(sock)) != 0) {
                CTRACE("res=%d", res);
            }
            break;

        case PGRES_POLLING_WRITING:
            if ((res = mrkthr_wait_for_write(sock)) != 0) {
                CTRACE("res=%d", res);
            }
            break;

        default:
            break;
        }
        if (!(pst == PGRES_POLLING_READING || pst == PGRES_POLLING_WRITING)) {
            break;
        }
    }
    //pqconn_dump_info(conn);



    if ((res = PQsendQuery(conn,
                           "select * from pg_catalog.pg_type;")) == 0) {
        CTRACE("PQsendQuery error: %s", PQerrorMessage(conn));
    }

    while ((res = PQflush(conn)) == 1) {
        int events;

        events = 0;
        if (mrkthr_wait_for_events(sock, &events) != 0) {
            break;
        }
        if (events & MRKTHR_WAIT_EVENT_READ) {
            if (PQconsumeInput(conn) == 0) {
                CTRACE("PQconsumeInput error: %s", PQerrorMessage(conn));
                break;
            }
        } else if (events & MRKTHR_WAIT_EVENT_WRITE) {
            continue;
        } else {
            break;
        }
    }


    while (true) {
        PGresult *qres;
        PQprintOpt popt;

        popt.header = false;
        popt.align = true;
        popt.standard = false;
        popt.html3 = false;
        popt.expanded = false;
        popt.pager = false;
        popt.fieldSep = "";
        popt.tableOpt = NULL;
        popt.caption = NULL;
        popt.fieldName = NULL;

        while (PQisBusy(conn)) {
            if ((res = mrkthr_wait_for_read(sock)) != 0) {
                CTRACE("mrkthr_wait_for_read res=%d", res);
                break;
            }
            if (PQconsumeInput(conn) == 0) {
                CTRACE("PQconsumeInput error: %s", PQerrorMessage(conn));
                break;
            }
        }

        if ((qres = PQgetResult(conn)) == NULL) {
            break;
        }

        CTRACE("qres=%p", qres);
        PQprint(stdout, qres, &popt);

        PQclear(qres);
    }

    PQfinish(conn);

}


static int
mycb1(UNUSED PGconn *conn,
      UNUSED PGresult *qres,
      UNUSED void *udata)
{
    struct {
        int i;
    } *params = udata;
    PQprintOpt popt;

    popt.header = false;
    popt.align = true;
    popt.standard = false;
    popt.html3 = false;
    popt.expanded = false;
    popt.pager = false;
    popt.fieldSep = "";
    popt.tableOpt = NULL;
    popt.caption = NULL;
    popt.fieldName = NULL;

    //CTRACE("qres=%p", qres);
    //CTRACE("ntuples=%d", PQntuples(qres));
    //CTRACE("nfields=%d", PQnfields(qres));
    //CTRACE("btuples=%d", PQbinaryTuples(qres));
    //CTRACE("nparams=%d", PQnparams(qres));
    //CTRACE("cmdstatus=%s", PQcmdStatus(qres));
    //CTRACE("cmdtuples=%s", PQcmdTuples(qres));
    //CTRACE("oidvalue=%d", PQoidValue(qres));
    //CTRACE("oidstatus=%s", PQoidStatus(qres));

    //PQprint(stdout, qres, &popt);

    if (params->i > 50) {
        PGcancel *cancel;
        char buf[256];

        if ((cancel = PQgetCancel(conn)) == NULL) {
            FAIL("PQgetCancel");
        }
        if (PQcancel(cancel, buf, sizeof(buf)) != 1) {
            CTRACE("PQcancel error: %s", buf);
        }
        PQfreeCancel(cancel);
        cancel = NULL;
    }

    ++params->i;
    return 0;
}


UNUSED static void
test_pgconn1(void)
{
    PGconn *conn;
    struct {
        int i;
    } params;

    if ((conn = mrkpq_connect_str(
                    "postgresql://postgres@localhost/postgres")) == NULL) {
        FAIL("mrkpq_connect_str");
    }

    params.i = 0;
    if (mrkpq_query(conn,
                    "select * from pg_catalog.pg_stats;",
                    MRKPQ_QUERY_SINGLE_ROW,
                    //0,
                    mycb1,
                    NULL,
                    &params) != 0) {
        FAIL("mrkpq_query");
    }
    CTRACE("i=%d", params.i);
    PQfinish(conn);
}


static int
mycb2(UNUSED PGconn *conn,
      UNUSED PGresult *qres,
      UNUSED void *udata)
{
    PQprintOpt popt;

    popt.header = false;
    popt.align = true;
    popt.standard = false;
    popt.html3 = false;
    popt.expanded = false;
    popt.pager = false;
    popt.fieldSep = "";
    popt.tableOpt = NULL;
    popt.caption = NULL;
    popt.fieldName = NULL;

    PQprint(stdout, qres, &popt);
    return 0;
}


UNUSED static void
test_pgconn2(void)
{
    PGconn *conn;
    const char * const params[] = {
        //"%_foreign_%",
        "pg_%",
    };

    if ((conn = mrkpq_connect_str(
                    "postgresql://postgres@localhost/postgres")) == NULL) {
        FAIL("mrkpq_connect_str");
    }

    //mrkthr_sleep(1000);

    if (mrkpq_prepare(conn,
                      "qweqwe",
                      "select * from pg_catalog.pg_tables where tablename like $1;",
                      0,
                      NULL,
                      0,
                      mycb2,
                      NULL,
                      NULL) != 0) {
        FAIL("mrkpq_prepare");
    }

    if (mrkpq_query_prepared(conn,
                             "qweqwe",
                             countof(params),
                             params,
                             NULL,
                             NULL,
                             0,
                             0,
                             mycb2,
                             NULL,
                             NULL) != 0) {
        FAIL("mrkpq_query_prepared");
    }

    PQfinish(conn);
}


static int
_test_pgconn3(UNUSED int argc, void **argv)
{
    mrkthr_sema_t *sema;
    PGconn *conn;
    intptr_t i;
    long n;
    char buf[32];
    const char * const params[] = {
        buf,
    };

    assert(argc == 3);

    sema = argv[0];
    conn = argv[1];
    i = (intptr_t)argv[2];
    n = random() % 1000;
    snprintf(buf, sizeof(buf), "%ld", n);
    CTRACE("i=%ld n=%ld", i, n);
    if (mrkthr_sema_acquire(sema) != 0) {
        goto err;
    }
    if (mrkpq_query_prepared(conn,
                             "asdasd",
                             countof(params),
                             params,
                             NULL,
                             NULL,
                             0,
                             0,
                             mycb2,
                             NULL,
                             NULL) != 0) {
        FAIL("mrkpq_query_prepared");
    }
    mrkthr_sema_release(sema);

    return 0;

err:
    return 1;
}


#define TEST_PGCONN3_N 10
UNUSED static void
test_pgconn3(void)
{
    int res;
    PGconn *conn;
    mrkthr_ctx_t *coros[TEST_PGCONN3_N];
    mrkthr_sema_t sema;
    struct {
        long rnd;
        int in;
        int expected;
    } data[] = {
        {0, 0, 0},
    };
    UNITTEST_PROLOG_RAND;

    mrkthr_sleep(200);
    if ((conn = mrkpq_connect_str(
                    "postgresql://postgres@localhost/postgres")) == NULL) {
        FAIL("mrkpq_connect_str");
    }

    if (mrkpq_prepare(conn,
                      "asdasd",
                      "select pg_sleep(0.0), $1::int;",
                      0,
                      NULL,
                      0,
                      mycb2,
                      NULL,
                      NULL) != 0) {
        FAIL("mrkpq_prepare");
    }

    mrkthr_sema_init(&sema, 1);
    for (i = 0; i < TEST_PGCONN3_N; ++i) {
        coros[i] = MRKTHR_SPAWN(NULL, _test_pgconn3, &sema, conn, (void *)(intptr_t)i);
    }

    for (i = 0; i < TEST_PGCONN3_N; ++i) {
        res = mrkthr_join(coros[i]);
        TR(res);
    }
    mrkthr_sema_fini(&sema);

    PQfinish(conn);
}


static int
run0(UNUSED int argc, UNUSED void **argv)
{
    CTRACE("argc=%d", argc);
    //mrkthr_sleep(2000);
    test_pgconn2();
    test_pgconn3();
    return 0;
}


UNUSED static void
test1(void)
{
    mrkthr_init();
    mrkthr_spawn("run0", run0, 0);
    mrkthr_loop();
    mrkthr_fini();
}


int
main(void)
{
    test0();
    test1();
    return 0;
}
