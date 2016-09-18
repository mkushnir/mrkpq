#ifndef MRKPQ_H_DEFINED
#define MRKPQ_H_DEFINED

#include <libpq-fe.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*mrkpq_result_cb_t)(PGconn *, PGresult *, void *);
typedef int (*mrkpq_notify_cb_t)(PGconn *, PGnotify *, void *);

PGconn *mrkpq_connect_str(const char *);
PGconn *mrkpq_connect_params(const char *const *,
                             const char *const *,
                             int);
int mrkpq_reset(PGconn *);

#define MRKPQ_QUERY_SINGLE_ROW (0x01)
int mrkpq_query(PGconn *,
                const char *,
                int,
                mrkpq_result_cb_t,
                mrkpq_notify_cb_t,
                void *);
int mrkpq_query_params(PGconn *,
                       const char *,
                       int,
                       const Oid *,
                       const char *const *,
                       const int *,
                       const int *,
                       int,
                       int,
                       mrkpq_result_cb_t,
                       mrkpq_notify_cb_t,
                       void *);
int mrkpq_prepare(PGconn *,
                  const char *,
                  const char *,
                  int,
                  const Oid *,
                  int,
                  mrkpq_result_cb_t,
                  mrkpq_notify_cb_t,
                  void *);
int mrkpq_query_prepared(PGconn *,
                         const char *,
                         int,
                         const char *const *,
                         const int *,
                         const int *,
                         int,
                         int,
                         mrkpq_result_cb_t,
                         mrkpq_notify_cb_t,
                         void *);



#ifdef __cplusplus
}
#endif
#endif /* MRKPQ_H_DEFINED */
