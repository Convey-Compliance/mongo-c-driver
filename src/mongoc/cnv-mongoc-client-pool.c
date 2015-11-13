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


#include "mongoc-counters-private.h"
#include "cnv-mongoc-client-pool-private.h"
#include "mongoc-queue-private.h"
#include "mongoc-thread-private.h"
#include "mongoc-cluster-private.h"
#include "mongoc-client-private.h"
#include "mongoc-trace.h"
#include "bson-atomic.h"

typedef struct _pooled_mongoc_client_t 
{
  struct _mongoc_client_t *client;
  int64_t insertionTime;
} _pooled_mongoc_client_t;

#ifdef MONGOC_ENABLE_SSL
void
cnv_mongoc_client_pool_set_ssl_opts (cnv_mongoc_client_pool_t   *pool,
                                     const mongoc_ssl_opt_t *opts)
{
   BSON_ASSERT (pool);

   mongoc_mutex_lock (&pool->mutex);

   memset (&pool->ssl_opts, 0, sizeof pool->ssl_opts);
   pool->ssl_opts_set = false;

   if (opts) {
      memcpy (&pool->ssl_opts, opts, sizeof pool->ssl_opts);
      pool->ssl_opts_set = true;

   }

   mongoc_topology_scanner_set_ssl_opts (pool->topology->scanner, &pool->ssl_opts);

   mongoc_mutex_unlock (&pool->mutex);
}
#endif

#define CLEANUP_POOL_MIN_THREADS 1
#define CLEANUP_POOL_MAX_THREADS 2
#define DEFAULT_CLEANUP_TIMER_INTERVAL 60000
#define DEFAULT_MAX_ALLOWED_IDLE_TIME (60000 * 5)

static SvcBusThreadPool cleanupPool;
static volatile long cleanupPoolInitCount = 0;

cnv_mongoc_client_pool_t *
cnv_mongoc_client_pool_new (const mongoc_uri_t *uri)
{
   mongoc_topology_t *topology;
   cnv_mongoc_client_pool_t *pool;

   ENTRY;

   BSON_ASSERT (uri);

   if(bson_atomic_int_add(&cleanupPoolInitCount, 1) == 1) {
     SvcBusThreadPool_init(&cleanupPool, CLEANUP_POOL_MIN_THREADS, CLEANUP_POOL_MAX_THREADS);
   }

   pool = (cnv_mongoc_client_pool_t*) bson_malloc0(sizeof *pool);
   mongoc_mutex_init(&pool->mutex);
   _mongoc_queue_init(&pool->queue);
   pool->uri = mongoc_uri_copy(uri);

   topology = mongoc_topology_new(uri, false);
   pool->topology = topology;

   SvcBusThreadPoolTimer_init(&pool->cleanupTimer);
   cnv_mongoc_client_pool_configure_cleanup_timer(pool, DEFAULT_CLEANUP_TIMER_INTERVAL, DEFAULT_MAX_ALLOWED_IDLE_TIME);

   mongoc_counter_client_pools_active_inc();

   RETURN(pool);
}

void
cnv_mongoc_client_pool_destroy (cnv_mongoc_client_pool_t *pool)
{
   mongoc_client_t *client;
   _pooled_mongoc_client_t *pooledClient;

   ENTRY;

   BSON_ASSERT(pool);

   SvcBusThreadPoolTimer_stop(&pool->cleanupTimer);
   SvcBusThreadPoolTimer_destroy(&pool->cleanupTimer);

   while ((pooledClient = (_pooled_mongoc_client_t*)_mongoc_queue_pop_head(&pool->queue))) {
     client = pooledClient->client;
     bson_free(pooledClient);
     mongoc_client_destroy(client);
   }

   mongoc_topology_destroy (pool->topology);

   mongoc_uri_destroy(pool->uri);

   if(bson_atomic_int_add(&cleanupPoolInitCount, -1) == 0) {
     SvcBusThreadPool_destroy(&cleanupPool);
   }
   mongoc_mutex_destroy(&pool->mutex);
   bson_free(pool);

   mongoc_counter_client_pools_active_dec();
   mongoc_counter_client_pools_disposed_inc();

   EXIT;
}

mongoc_client_t *
cnv_mongoc_client_pool_pop (cnv_mongoc_client_pool_t *pool)
{
   mongoc_client_t *client;
   _pooled_mongoc_client_t* pooledClient;

   ENTRY;

   BSON_ASSERT(pool);

   mongoc_mutex_lock(&pool->mutex);

   if (!(pooledClient = (_pooled_mongoc_client_t *) _mongoc_queue_pop_head(&pool->queue))) {
     client = mongoc_client_new_from_uri(pool->uri, pool->topology);
#ifdef MONGOC_ENABLE_SSL
     if (pool->ssl_opts_set) {
       mongoc_client_set_ssl_opts (client, &pool->ssl_opts);
     }
#endif
   } else {
     client = pooledClient->client;
     bson_free(pooledClient);
   }

   mongoc_mutex_unlock(&pool->mutex);

   RETURN(client);
}

void
cnv_mongoc_client_pool_push (cnv_mongoc_client_pool_t *pool,
                             mongoc_client_t          *client)
{
   _pooled_mongoc_client_t* pooledClient;

   ENTRY;

   BSON_ASSERT(pool);
   BSON_ASSERT(client);

   pooledClient = (_pooled_mongoc_client_t*) bson_malloc(sizeof *pooledClient);
   pooledClient->client = client;
   pooledClient->insertionTime = bson_get_monotonic_time();

   mongoc_mutex_lock (&pool->mutex);
   _mongoc_queue_push_tail (&pool->queue, pooledClient);
   mongoc_mutex_unlock(&pool->mutex);

   EXIT;
}

static void cleanupIdleConnections(void* context) 
{
  cnv_mongoc_client_pool_t * pool = (cnv_mongoc_client_pool_t *) context;
  mongoc_queue_t clientsToDispose;
  mongoc_client_t* client;
  _pooled_mongoc_client_t *pooledClient;
  uint32_t pooledElementsCount;

  ENTRY;

  _mongoc_queue_init(&clientsToDispose);

  mongoc_mutex_lock(&pool->mutex);
  pooledElementsCount = _mongoc_queue_get_length(&pool->queue);

  while (pooledElementsCount-- > 0) {
    pooledClient = (_pooled_mongoc_client_t*)_mongoc_queue_pop_head(&pool->queue);
    if(bson_get_monotonic_time() - pooledClient->insertionTime > pool->maxIdleMillis * 1000L) {
      _mongoc_queue_push_head(&clientsToDispose, pooledClient->client);
      bson_free(pooledClient);
    } else {
      _mongoc_queue_push_tail(&pool->queue, pooledClient);
    }
  }
  mongoc_mutex_unlock(&pool->mutex);

  while((client = (mongoc_client_t*) _mongoc_queue_pop_head(&clientsToDispose))) {
    mongoc_client_destroy(client);
  }

  EXIT;
}

void
cnv_mongoc_client_pool_configure_cleanup_timer (cnv_mongoc_client_pool_t *pool,
                                                unsigned int             timerIntervalMillis,
                                                unsigned int             maxIdleMillis)
{
  SvcBusThreadPoolTimer_stop(&pool->cleanupTimer);
  pool->maxIdleMillis = maxIdleMillis;
  SvcBusThreadPoolTimer_start(&pool->cleanupTimer, &cleanupPool, &cleanupIdleConnections, pool, timerIntervalMillis);
}
