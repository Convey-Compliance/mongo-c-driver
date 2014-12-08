#include "mongoc-stream-cnv-gridfs.h"
#include "mongoc-stream-gridfs.h"
#include "mongoc-stream-private.h"

#include <zlib.h>

typedef enum
{
   NOT_INITED,
   COMPRESSING,
   UNCOMPRESSING
} zlib_state_t;

typedef struct
{
   mongoc_stream_t       stream;
   mongoc_stream_t      *gridfs_stream;
   int                   cnv_flags;
   z_stream              zlib_stream;
   zlib_state_t          zlib_inited;
   unsigned char        *pending_compressed_data;
} mongoc_stream_cnv_gridfs_t;


#define ZLIB_STREAM_END_MARK_SIZE 6


#define CHECK_IOVCNT \
   if (iovcnt > 1) { \
      fprintf (stderr, "Only iovcnt == 1 supported"); \
      abort (); \
   }


static void
_mongoc_stream_cnv_gridfs_destroy (mongoc_stream_t *stream)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   mongoc_stream_close (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;
   mongoc_stream_destroy (cnvs->gridfs_stream);
   if (cnvs->pending_compressed_data)
      bson_free (cnvs->pending_compressed_data);
   bson_free (cnvs);
}


static ssize_t
write_zlib_stream_end_mark (mongoc_stream_cnv_gridfs_t *cnvs) {
   ssize_t ret;
   char buf[ZLIB_STREAM_END_MARK_SIZE];
   mongoc_iovec_t iov = { sizeof buf, buf };

   cnvs->zlib_stream.avail_out = sizeof buf;
   cnvs->zlib_stream.next_out = buf;
   ret = deflate (&cnvs->zlib_stream, Z_FINISH);
   if (ret != Z_STREAM_END)
      return ret;

   return mongoc_stream_writev (cnvs->gridfs_stream, &iov, 1, 0);
}


static int
_mongoc_stream_cnv_gridfs_close (mongoc_stream_t *stream)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;

   if (cnvs->zlib_inited == COMPRESSING) {
      BSON_ASSERT (ZLIB_STREAM_END_MARK_SIZE == write_zlib_stream_end_mark (cnvs));
      (void)deflateEnd (&cnvs->zlib_stream);
   }
   if (cnvs->zlib_inited == UNCOMPRESSING)
      (void)inflateEnd(&cnvs->zlib_stream);
   cnvs->zlib_inited = NOT_INITED;

   return mongoc_stream_close (cnvs->gridfs_stream);
}


static int
_mongoc_stream_cnv_gridfs_flush (mongoc_stream_t *stream)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;

   return mongoc_stream_flush (cnvs->gridfs_stream);
}


static ssize_t
_uncompress_pending_compressed_data (mongoc_stream_cnv_gridfs_t *cnvs,
                                     char                       *uncompressed,
                                     size_t                      uncompressed_len,
                                     size_t                      min_bytes,
                                     int32_t                     timeout_msec)
{
   ssize_t ret;

   if (cnvs->zlib_inited == NOT_INITED) {
      ret = inflateInit(&cnvs->zlib_stream);
      if (ret != Z_OK)
         return ret;
      cnvs->zlib_inited = UNCOMPRESSING;
   }

   cnvs->zlib_stream.avail_out = uncompressed_len;
   cnvs->zlib_stream.next_out = uncompressed;
   ret = inflate(&cnvs->zlib_stream, Z_SYNC_FLUSH);

   return ret == Z_OK || ret == Z_STREAM_END ? uncompressed_len - cnvs->zlib_stream.avail_out : ret;
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

   if (cnvs->cnv_flags & MONGOC_CNV_COMPRESS) {
      CHECK_IOVCNT

      if (cnvs->zlib_stream.avail_in == 0) { // read next portion of compressed data
         ssize_t nread_compressed;

         cnvs->pending_compressed_data = bson_realloc (cnvs->pending_compressed_data, iov->iov_len);

         nread_compressed = mongoc_stream_read (cnvs->gridfs_stream, cnvs->pending_compressed_data, iov->iov_len, min_bytes, timeout_msec);
         if (nread_compressed <= 0)
            return nread_compressed;

         cnvs->zlib_stream.avail_in = nread_compressed;
         cnvs->zlib_stream.next_in = cnvs->pending_compressed_data;
      }
      return _uncompress_pending_compressed_data (cnvs, iov->iov_base, iov->iov_len, min_bytes, timeout_msec);
   }

   return mongoc_stream_readv (cnvs->gridfs_stream, iov, iovcnt, min_bytes, timeout_msec);
}


static ssize_t
_compress_buf (mongoc_stream_cnv_gridfs_t *cnvs,
               char                       *buf,
               size_t                      buf_len,
               char                       *compressed,
               size_t                      compressed_len)
{
   ssize_t ret;

   cnvs->zlib_stream.avail_in = buf_len;
   cnvs->zlib_stream.next_in = buf;

   cnvs->zlib_stream.avail_out = compressed_len;
   cnvs->zlib_stream.next_out = compressed;
   ret = deflate(&cnvs->zlib_stream, Z_SYNC_FLUSH);
   assert(cnvs->zlib_stream.avail_in == 0);

   return (ret == Z_OK || ret == Z_STREAM_END) ? compressed_len - cnvs->zlib_stream.avail_out : ret;
}


static ssize_t
_mongoc_stream_cnv_gridfs_writev (mongoc_stream_t *stream,
                                  mongoc_iovec_t  *iov,
                                  size_t           iovcnt,
                                  int32_t          timeout_msec)
{
   mongoc_stream_cnv_gridfs_t *cnvs;

   BSON_ASSERT (stream);

   cnvs = (mongoc_stream_cnv_gridfs_t *)stream;

   if (cnvs->cnv_flags & MONGOC_CNV_COMPRESS) {
      ssize_t ret;
      size_t compressed_len;
      mongoc_iovec_t compressed;

      CHECK_IOVCNT

      if (cnvs->zlib_inited == NOT_INITED) {
         ret = deflateInit(&cnvs->zlib_stream, Z_BEST_SPEED);
         if (ret != Z_OK)
            return ret;
         cnvs->zlib_inited = COMPRESSING;
      }

      compressed_len = deflateBound (&cnvs->zlib_stream, iov->iov_len);
      cnvs->pending_compressed_data = bson_realloc (cnvs->pending_compressed_data, compressed_len);

      ret = _compress_buf (cnvs, iov->iov_base, iov->iov_len, cnvs->pending_compressed_data, compressed_len);
      if (ret <= 0)
         return ret;

      compressed.iov_base = cnvs->pending_compressed_data;
      compressed.iov_len = ret;

      return mongoc_stream_writev (cnvs->gridfs_stream, &compressed, iovcnt, timeout_msec);
   }

   return mongoc_stream_writev (cnvs->gridfs_stream, iov, iovcnt, timeout_msec);
}


mongoc_stream_t *
mongoc_stream_cnv_gridfs_new (mongoc_gridfs_file_t *file, 
                              int                   cnv_flags)
{
   mongoc_stream_cnv_gridfs_t *stream;
   mongoc_stream_t *gridfs_stream;

   gridfs_stream = mongoc_stream_gridfs_new (file);
   if (!gridfs_stream)
      return NULL;

   stream = bson_malloc0 (sizeof *stream);
   stream->stream.type = MONGOC_STREAM_GRIDFS;
   stream->stream.destroy = _mongoc_stream_cnv_gridfs_destroy;
   stream->stream.close = _mongoc_stream_cnv_gridfs_close;
   stream->stream.flush = _mongoc_stream_cnv_gridfs_flush;
   stream->stream.writev = _mongoc_stream_cnv_gridfs_writev;
   stream->stream.readv = _mongoc_stream_cnv_gridfs_readv;
   stream->gridfs_stream = gridfs_stream;
   stream->cnv_flags = cnv_flags;
   stream->zlib_inited = NOT_INITED;
   stream->pending_compressed_data = NULL;

   stream->zlib_stream.zalloc = Z_NULL;
   stream->zlib_stream.zfree = Z_NULL;
   stream->zlib_stream.opaque = Z_NULL;
   stream->zlib_stream.avail_in = 0;
   stream->zlib_stream.next_in = Z_NULL;

   return ((mongoc_stream_t *)stream);
}
