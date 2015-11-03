/*
 * Copyright 2015 MongoDB, Inc.
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

#ifndef CNV_MONGOC_CLIENT_POOL_PRIVATE_H
#define CNV_MONGOC_CLIENT_POOL_PRIVATE_H

#if !defined (MONGOC_I_AM_A_DRIVER) && !defined (MONGOC_COMPILATION)
#error "Only <mongoc.h> can be included directly."
#endif

#include <bson.h>

#include "cnv-mongoc-client-pool.h"
#include "mongoc-queue-private.h"

BSON_BEGIN_DECLS


/* the following declarations are to avoid including "mongoc-thread-private.h" */
#if !defined(_WIN32)
# include <pthread.h>
# define mongoc_mutex_t                 pthread_mutex_t
# define mongoc_cond_t                  pthread_cond_t
#else
# define mongoc_mutex_t                 CRITICAL_SECTION
# define mongoc_cond_t                  CONDITION_VARIABLE
#endif

struct _cnv_mongoc_client_pool_t
{
  mongoc_mutex_t    mutex;
  mongoc_cond_t     cond;
  mongoc_queue_t    queue;
  mongoc_uri_t     *uri;
  uint32_t          min_pool_size;
  uint32_t          max_pool_size;
  uint32_t          size;
#ifdef MONGOC_ENABLE_SSL
  bool              ssl_opts_set;
  mongoc_ssl_opt_t  ssl_opts;
#endif
};

size_t cnv_mongoc_client_pool_get_size(cnv_mongoc_client_pool_t *pool);

BSON_END_DECLS


#endif /* CNV_MONGOC_CLIENT_POOL_PRIVATE_H */
