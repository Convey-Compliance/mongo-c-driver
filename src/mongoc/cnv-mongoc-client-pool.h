/*
 * Copyright 2013 MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CNV_MONGOC_CLIENT_POOL_H
#define CNV_MONGOC_CLIENT_POOL_H

#include "mongoc-client.h"
#include "mongoc-config.h"
#ifdef MONGOC_ENABLE_SSL
# include "mongoc-ssl.h"
#endif
#include "mongoc-uri.h"


BSON_BEGIN_DECLS


typedef struct _cnv_mongoc_client_pool_t cnv_mongoc_client_pool_t;


cnv_mongoc_client_pool_t *cnv_mongoc_client_pool_new     (const mongoc_uri_t   *uri);
void                  cnv_mongoc_client_pool_destroy (cnv_mongoc_client_pool_t *pool);
mongoc_client_t      *cnv_mongoc_client_pool_pop     (cnv_mongoc_client_pool_t *pool);
void                  cnv_mongoc_client_pool_push    (cnv_mongoc_client_pool_t *pool,
                                                  mongoc_client_t      *client);
#ifdef MONGOC_ENABLE_SSL
void                  cnv_mongoc_client_pool_set_ssl_opts (cnv_mongoc_client_pool_t   *pool,
                                                           const mongoc_ssl_opt_t *opts);
#endif
void                  cnv_mongoc_client_pool_configure_cleanup_timer (cnv_mongoc_client_pool_t *pool,
                                                                      unsigned int timerIntervalMillis,
                                                                      unsigned int maxIdleMillis);


BSON_END_DECLS


#endif /* CNV_MONGOC_CLIENT_POOL_H */
