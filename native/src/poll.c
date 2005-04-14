/* Copyright 2000-2004 The Apache Software Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_poll.h"
#include "tcn.h"

/* Internal poll structure for queryset
 */

typedef struct tcn_pollset {
    apr_pool_t    *pool;
    apr_int32_t   nelts;
    apr_int32_t   nalloc;
    apr_pollset_t *pollset;
    apr_pollfd_t  *query_set;
    apr_time_t    *query_add;
    apr_interval_time_t max_ttl;
} tcn_pollset_t;

TCN_IMPLEMENT_CALL(jlong, Poll, create)(TCN_STDARGS, jint size,
                                        jlong pool, jint flags,
                                        jlong ttl)
{
    apr_pool_t *p = J2P(pool, apr_pool_t *);
    apr_pollset_t *pollset = NULL;
    tcn_pollset_t *tps = NULL;
    UNREFERENCED(o);

    TCN_THROW_IF_ERR(apr_pollset_create(&pollset,
                     (apr_uint32_t)size, p, (apr_uint32_t)flags),
                     pollset);

    tps = apr_palloc(p, sizeof(tcn_pollset_t));
    tps->pollset = pollset;
    tps->query_set = apr_palloc(p, size * sizeof(apr_pollfd_t));
    tps->query_add = apr_palloc(p, size * sizeof(apr_time_t));
    tps->nelts  = 0;
    tps->nalloc = size;
    tps->pool   = p;
    tps->max_ttl = J2T(ttl);

cleanup:
    return P2J(tps);

}

TCN_IMPLEMENT_CALL(jint, Poll, destroy)(TCN_STDARGS, jlong pollset)
{
    tcn_pollset_t *p = J2P(pollset,  tcn_pollset_t *);;

    UNREFERENCED_STDARGS;;
    return (jint)apr_pollset_destroy(p->pollset);
}

TCN_IMPLEMENT_CALL(jint, Poll, add)(TCN_STDARGS, jlong pollset,
                                    jlong socket, jlong data,
                                    jint reqevents)
{
    tcn_pollset_t *p = J2P(pollset,  tcn_pollset_t *);
    apr_pollfd_t fd;
    apr_status_t rv;

    UNREFERENCED_STDARGS;
    if (p->nelts == p->nalloc) {
        return APR_ENOMEM;
    }

    memset(&fd, 0, sizeof(apr_pollfd_t));
    fd.desc_type = APR_POLL_SOCKET;
    fd.reqevents = (apr_int16_t)reqevents;
    fd.desc.s = J2P(socket, apr_socket_t *);
    fd.client_data = J2P(data, void *);
    if ((rv = apr_pollset_add(p->pollset, &fd)) == APR_SUCCESS) {
        p->query_set[p->nelts] = fd;
        p->query_add[p->nelts] = apr_time_now();
        p->nelts++;
    }
    return (jint)rv;
}

TCN_IMPLEMENT_CALL(jint, Poll, remove)(TCN_STDARGS, jlong pollset,
                                       jlong socket)
{
    tcn_pollset_t *p = J2P(pollset,  tcn_pollset_t *);
    apr_pollfd_t fd;
    apr_int32_t i;

    UNREFERENCED_STDARGS;;

    memset(&fd, 0, sizeof(apr_pollfd_t));
    fd.desc_type = APR_POLL_SOCKET;
    fd.desc.s = J2P(socket, apr_socket_t *);

    for (i = 0; i < p->nelts; i++) {
        if (fd.desc.s == p->query_set[i].desc.s) {
            /* Found an instance of the fd: remove this and any other copies */
            apr_int32_t dst = i;
            apr_int32_t old_nelts = p->nelts;
            p->nelts--;
            for (i++; i < old_nelts; i++) {
                if (fd.desc.s == p->query_set[i].desc.s) {
                    p->nelts--;
                }
                else {
                    p->query_set[dst] = p->query_set[i];
                    dst++;
                }
            }
            break;
        }
    }

    return (jint)apr_pollset_remove(p->pollset, &fd);
}

TCN_IMPLEMENT_CALL(jint, Poll, poll)(TCN_STDARGS, jlong pollset,
                                     jlong timeout, jlongArray set)
{
    const apr_pollfd_t *fd = NULL;
    tcn_pollset_t *p = J2P(pollset,  tcn_pollset_t *);
    jlong *pset = (*e)->GetLongArrayElements(e, set, NULL);
    apr_int32_t  n, i = 0, num = 0;
    apr_status_t rv;

    UNREFERENCED(o);
    rv = apr_pollset_poll(p->pollset, J2T(timeout), &num, &fd);

    if (rv == APR_SUCCESS && num > 0) {
        for (i = 0; i < num; i++) {
            pset[i] = P2J(fd);
            fd ++;
        }
    }
    /* In any case check for timeout sockets */
    if (p->max_ttl > 0) {
        apr_time_t now = apr_time_now();
        /* TODO: Add thread mutex protection
         * or make sure the Java part is synchronized.
         */
        for (n = 0; n < p->nelts; n++) {
            if ((now - p->query_add[n]) > p->max_ttl) {
                p->query_set[n].rtnevents = APR_POLLHUP | APR_POLLIN;
                if (i < p->nelts) {
                    pset[i++] = P2J(&(p->query_set[n]));
                    num++;
                }
            }
        }
    }
    if (num)
        (*e)->ReleaseLongArrayElements(e, set, pset, 0);
    else
        (*e)->ReleaseLongArrayElements(e, set, pset, JNI_ABORT);

    return (jint)num;
}

TCN_IMPLEMENT_CALL(jlong, Poll, socket)(TCN_STDARGS, jlong pollfd)
{
    apr_pollfd_t *fd = J2P(pollfd,  apr_pollfd_t *);
    UNREFERENCED_STDARGS;;
    return P2J(fd->desc.s);
}

TCN_IMPLEMENT_CALL(jlong, Poll, data)(TCN_STDARGS, jlong pollfd)
{
    apr_pollfd_t *fd = J2P(pollfd,  apr_pollfd_t *);
    UNREFERENCED_STDARGS;;
    return P2J(fd->client_data);
}

TCN_IMPLEMENT_CALL(jint, Poll, events)(TCN_STDARGS, jlong pollfd)
{
    apr_pollfd_t *fd = J2P(pollfd,  apr_pollfd_t *);
    UNREFERENCED_STDARGS;;
    return (jint)fd->rtnevents;
}
