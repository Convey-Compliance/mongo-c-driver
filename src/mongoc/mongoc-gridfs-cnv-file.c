#include "mongoc-gridfs-cnv-file.h"
#include "mongoc-gridfs-file-private.h"
#include <zlib.h>


static void
after_chunk_read (mongoc_gridfs_file_t *file, const uint8_t **data, uint32_t *len)
{
   mongoc_gridfs_cnv_file_t *cnv_file = (mongoc_gridfs_cnv_file_t *)file->chunk_callbacks_custom_data;

   if (cnv_file->flags & MONGOC_CNV_UNCOMPRESS) {
      uint32_t uncompressed_len = file->chunk_size;

      BSON_ASSERT (Z_OK == uncompress (cnv_file->buf_for_compress, &uncompressed_len, *data, *len));

      *data = cnv_file->buf_for_compress;
      *len = uncompressed_len;
   } else if (*len > file->chunk_size) {
      /* this needed when we reading compressed file without uncompressing it
         mongo implementation assumes that chunk size can't be > file->chunk_size and
         mongoc_gridfs_file_get_length and mongoc_gridfs_file_readv returns invalid values
         however compressed chunk size can be > file->chunk_size
         so we should fix file->length to satisfy mongo implementation
         and we remember overheads, so mongoc_gridfs_cnv_file_get_length and mongoc_gridfs_cnv_file_readv returns correct values
         */
      uint32_t chunk_overhead = *len - file->chunk_size;
      file->length -= chunk_overhead;
      cnv_file->read_length_fix += chunk_overhead;
      cnv_file->length_fix += chunk_overhead;
   }
}

static void
before_chunk_write (mongoc_gridfs_file_t *file, const uint8_t **data, uint32_t *len)
{
   mongoc_gridfs_cnv_file_t *cnv_file = (mongoc_gridfs_cnv_file_t *)file->chunk_callbacks_custom_data;

   if (cnv_file->flags & MONGOC_CNV_COMPRESS) {
      uint32_t compressed_len = compressBound (file->chunk_size);

      BSON_ASSERT (Z_OK == compress2 (cnv_file->buf_for_compress, &compressed_len, *data, *len, Z_BEST_SPEED));
      cnv_file->compressed_length += compressed_len;
      if (cnv_file->need_to_append_compressed_len) {
        /* we can't do this inside save() cause last before_chunk_write() called after save() and we have no final compressed_length at this point */
        bson_append_int64 (mongoc_gridfs_cnv_file_get_metadata (cnv_file), "compressed_length", -1, cnv_file->compressed_length);
        cnv_file->need_to_append_compressed_len = 0;
      }

      *data = cnv_file->buf_for_compress;
      *len = compressed_len;
   }
}

mongoc_gridfs_file_chunk_callbacks_t callbacks = { after_chunk_read, before_chunk_write };


static void
load_metadata (mongoc_gridfs_cnv_file_t *cnv_file)
{
   bson_t *metadata = (bson_t *)mongoc_gridfs_file_get_metadata (cnv_file->file);
   bson_iter_t it;

   if (!metadata)
      return;

   if (bson_iter_init_find (&it, metadata, "compressed_length"))
      cnv_file->compressed_length = bson_iter_int64 (&it);
   if (!(cnv_file->flags & MONGOC_CNV_UNCOMPRESS) && mongoc_gridfs_cnv_file_is_compressed (cnv_file))
      /* need to set size to be actual size in case reading compressed file without uncompress
         cause implementation depends on this */
      cnv_file->file->length = cnv_file->compressed_length;
}

static mongoc_gridfs_cnv_file_t *
mongoc_gridfs_cnv_file_new (mongoc_gridfs_file_t *file, mongoc_gridfs_cnv_file_flags_t flags)
{
   mongoc_gridfs_cnv_file_t *cnv_file;

   if (!file)
      return NULL;

   cnv_file = bson_malloc0 (sizeof (mongoc_gridfs_cnv_file_t));
   cnv_file->file = file;
   cnv_file->flags = flags;
   cnv_file->compressed_length = 0;
   cnv_file->length_fix = 0;
   cnv_file->need_to_append_compressed_len = 0;
   cnv_file->buf_for_compress = cnv_file->flags & MONGOC_CNV_COMPRESS || cnv_file->flags & MONGOC_CNV_UNCOMPRESS ?
     bson_malloc0 (compressBound (file->chunk_size)) : NULL;
   cnv_file->file->chunk_callbacks_custom_data = cnv_file;
   mongoc_gridfs_file_set_chunk_callbacks (file, &callbacks);

   load_metadata (cnv_file);

   return cnv_file;
}

mongoc_gridfs_cnv_file_t *
mongoc_gridfs_create_cnv_file_from_stream (mongoc_gridfs_t                *gridfs,
                                           mongoc_stream_t                *stream,
                                           mongoc_gridfs_file_opt_t       *opt,
                                           mongoc_gridfs_cnv_file_flags_t  flags)
{
   return mongoc_gridfs_cnv_file_new (mongoc_gridfs_create_file_from_stream (gridfs, stream, opt), flags);
}

mongoc_gridfs_cnv_file_t *
mongoc_gridfs_create_cnv_file (mongoc_gridfs_t                *gridfs,
                               mongoc_gridfs_file_opt_t       *opt,
                               mongoc_gridfs_cnv_file_flags_t  flags)
{
   return mongoc_gridfs_cnv_file_new (mongoc_gridfs_create_file (gridfs, opt), flags);
}

mongoc_gridfs_cnv_file_t *mongoc_gridfs_find_one_cnv (mongoc_gridfs_t                *gridfs,
                                                      const bson_t                   *query,
                                                      bson_error_t                   *error,
                                                      mongoc_gridfs_cnv_file_flags_t  flags)
{
   return mongoc_gridfs_cnv_file_new (mongoc_gridfs_find_one (gridfs, query, error), flags);
}

mongoc_gridfs_cnv_file_t *mongoc_gridfs_find_one_cnv_by_filename (mongoc_gridfs_t                *gridfs,
                                                                  const char                     *filename,
                                                                  bson_error_t                   *error,
                                                                  mongoc_gridfs_cnv_file_flags_t  flags)
{
   return mongoc_gridfs_cnv_file_new (mongoc_gridfs_find_one_by_filename (gridfs, filename, error), flags);
}


#define MONGOC_GRIDFS_CNV_FILE_STR_ACCESSOR(name) \
   const char * \
   mongoc_gridfs_cnv_file_get_##name (mongoc_gridfs_cnv_file_t * file) \
   { \
      return mongoc_gridfs_file_get_##name (file->file); \
   } \
   void \
   mongoc_gridfs_cnv_file_set_##name (mongoc_gridfs_cnv_file_t *file, \
                                      const char               *str) \
   { \
      mongoc_gridfs_file_set_##name (file->file, str); \
   } \

#define MONGOC_GRIDFS_CNV_FILE_BSON_ACCESSOR(name) \
   const bson_t * \
   mongoc_gridfs_cnv_file_get_##name (mongoc_gridfs_cnv_file_t * file) \
   { \
      return mongoc_gridfs_file_get_##name (file->file); \
   } \
   void \
   mongoc_gridfs_cnv_file_set_##name (mongoc_gridfs_cnv_file_t * file, \
                                      const bson_t             * bson) \
   { \
      mongoc_gridfs_file_set_##name (file->file, bson); \
   } \

MONGOC_GRIDFS_CNV_FILE_STR_ACCESSOR  (md5)
MONGOC_GRIDFS_CNV_FILE_STR_ACCESSOR  (filename)
MONGOC_GRIDFS_CNV_FILE_STR_ACCESSOR  (content_type)
MONGOC_GRIDFS_CNV_FILE_BSON_ACCESSOR (aliases)
MONGOC_GRIDFS_CNV_FILE_BSON_ACCESSOR (metadata)

void
mongoc_gridfs_cnv_file_destroy (mongoc_gridfs_cnv_file_t *file)
{
   mongoc_gridfs_file_destroy (file->file);
   if(file->flags & MONGOC_CNV_COMPRESS || file->flags & MONGOC_CNV_UNCOMPRESS)
      bson_free (file->buf_for_compress);
   bson_free (file);
}

ssize_t
mongoc_gridfs_cnv_file_readv (mongoc_gridfs_cnv_file_t *file,
                              mongoc_iovec_t           *iov,
                              size_t                    iovcnt,
                              size_t                    min_bytes,
                              uint32_t                  timeout_msec)
{
   ssize_t ret = mongoc_gridfs_file_readv (file->file, iov, iovcnt, min_bytes, timeout_msec);
   if (ret > 0) {
      ret += file->read_length_fix;
      file->read_length_fix = 0;
   }
   return ret;
}

ssize_t
mongoc_gridfs_cnv_file_writev (mongoc_gridfs_cnv_file_t *file,
                               mongoc_iovec_t           *iov,
                               size_t                    iovcnt,
                               uint32_t                  timeout_msec)
{
   return mongoc_gridfs_file_writev (file->file, iov, iovcnt, timeout_msec);
}

int
mongoc_gridfs_cnv_file_seek (mongoc_gridfs_cnv_file_t *file,
                             uint64_t                  delta,
                             int                       whence)
{
   return mongoc_gridfs_file_seek (file->file, delta, whence);
}

uint64_t
mongoc_gridfs_cnv_file_tell (mongoc_gridfs_cnv_file_t *file)
{
   return mongoc_gridfs_file_tell (file->file);
}


int64_t
mongoc_gridfs_cnv_file_get_length (mongoc_gridfs_cnv_file_t *file)
{
   return mongoc_gridfs_file_get_length (file->file) + file->length_fix;
}

int32_t
mongoc_gridfs_cnv_file_get_chunk_size (mongoc_gridfs_cnv_file_t *file)
{
   return mongoc_gridfs_file_get_chunk_size (file->file);
}

int64_t
mongoc_gridfs_cnv_file_get_upload_date (mongoc_gridfs_cnv_file_t *file)
{
   return mongoc_gridfs_file_get_upload_date (file->file);
}

bool
mongoc_gridfs_cnv_file_save (mongoc_gridfs_cnv_file_t *file)
{
   bson_t metadata = BSON_INITIALIZER;
   mongoc_gridfs_file_set_metadata (file->file, &metadata);
   file->need_to_append_compressed_len = 1;
   return mongoc_gridfs_file_save (file->file);
}

bool
mongoc_gridfs_cnv_file_remove (mongoc_gridfs_cnv_file_t *file,
                               bson_error_t             *error)
{
   return mongoc_gridfs_file_remove (file->file, error);
}

bool
mongoc_gridfs_cnv_file_is_compressed (mongoc_gridfs_cnv_file_t *file)
{
   return file->compressed_length > 0;
}

int64_t
mongoc_gridfs_cnv_file_get_compressed_length (mongoc_gridfs_cnv_file_t *file)
{
   return file->compressed_length;
}
