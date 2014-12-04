#include "mongoc-stream-cnv-gridfs.h"
#include "mongoc-stream-gridfs.h"
#include "mongoc-stream-private.h"

#include <zlib.h>

typedef struct
{
   mongoc_stream_t       stream;
   mongoc_stream_t      *gs;
   int flags;
} mongoc_stream_cnv_gridfs_t;


static void
_mongoc_stream_cnv_gridfs_destroy (mongoc_stream_t *stream)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;
   mongoc_stream_destroy (cnvs->gs);

   bson_free (cnvs);
}


static int
_mongoc_stream_cnv_gridfs_close (mongoc_stream_t *stream)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;

   return mongoc_stream_close (cnvs->gs);
}


static int
_mongoc_stream_cnv_gridfs_flush (mongoc_stream_t *stream)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;

   return mongoc_stream_flush (cnvs->gs);
}


static ssize_t
_mongoc_stream_cnv_gridfs_readv (mongoc_stream_t *stream,
                                 mongoc_iovec_t  *iov,
                                 size_t           iovcnt,
                                 size_t           min_bytes,
                                 int32_t          timeout_msec)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;

   return mongoc_stream_readv (cnvs->gs, iov, iovcnt, min_bytes, timeout_msec);
}


static ssize_t
_mongoc_stream_cnv_gridfs_writev (mongoc_stream_t *stream,
                                  mongoc_iovec_t  *iov,
                                  size_t           iovcnt,
                                  int32_t     timeout_msec)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;

   return mongoc_stream_writev (cnvs->gs, iov, iovcnt, timeout_msec);
}


mongoc_stream_t *
mongoc_stream_cnv_gridfs_new (mongoc_gridfs_file_t *file, 
                              int                   cnv_flags)
{
   mongoc_stream_cnv_gridfs_t *stream;
   mongoc_stream_t *gs;

   gs = mongoc_stream_gridfs_new (file);
   if (!gs)
      return NULL;

   stream = bson_malloc0 (sizeof *stream);
   stream->stream.type = MONGOC_STREAM_GRIDFS;
   stream->stream.destroy = _mongoc_stream_cnv_gridfs_destroy;
   stream->stream.close = _mongoc_stream_cnv_gridfs_close;
   stream->stream.flush = _mongoc_stream_cnv_gridfs_flush;
   stream->stream.writev = _mongoc_stream_cnv_gridfs_writev;
   stream->stream.readv = _mongoc_stream_cnv_gridfs_readv;
   stream->gs = gs;
   stream->flags = cnv_flags;

   return ((mongoc_stream_t *)stream);
}
