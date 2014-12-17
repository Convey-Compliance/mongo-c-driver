#include "mongoc-gridfs-cnv-file.h"
#include "mongoc-gridfs-file-private.h"
#include "mongoc-gridfs-file-page-private.h"
#include <zlib.h>


const char * const MONGOC_CNV_GRIDFS_FILE_COMPRESSED_LEN = "compressed_len",
           * const MONGOC_CNV_GRIDFS_FILE_AES_IV = "aes_initialization_vector",
           * const MONGOC_CNV_GRIDFS_FILE_AES_TAG = "aes_tag",
           * const MONGOC_CNV_GRIDFS_FILE_AES_SALT = "aes_salt",
           * const ERR_MSG_INVALID_AES_KEY = "Invalid AES key",
           * const ERR_MSG_AES_EAX_INTEGRITY_CHECK_FAILED = "Encrypted or decrypted data is corrupted or wrong key\\password used";

static void
fill_buf_with_rand (unsigned char *buf, size_t len)
{
   libscrypt_salt_gen (buf, len);
}


static void
append_metadata (mongoc_gridfs_cnv_file_t *file)
{
   bson_t *metadata = (bson_t*)mongoc_gridfs_cnv_file_get_metadata (file);

   if (file->flags & MONGOC_CNV_ENCRYPT) {
      unsigned char tag[AES_BLOCK_SIZE];

      eax_compute_tag (tag, sizeof tag, &file->aes_ctx);
      bson_append_binary (metadata, MONGOC_CNV_GRIDFS_FILE_AES_TAG, -1, BSON_SUBTYPE_BINARY, 
                          tag, sizeof tag);

      bson_append_binary (metadata, MONGOC_CNV_GRIDFS_FILE_AES_IV, -1, BSON_SUBTYPE_BINARY, 
                          file->aes_initialization_vector, sizeof file->aes_initialization_vector);
      bson_append_binary (metadata, MONGOC_CNV_GRIDFS_FILE_AES_SALT, -1, BSON_SUBTYPE_BINARY, 
                          file->salt, sizeof file->salt);
   }
   if (file->flags & MONGOC_CNV_COMPRESS)
      bson_append_int64 ((bson_t*)mongoc_gridfs_cnv_file_get_metadata (file),
                         MONGOC_CNV_GRIDFS_FILE_COMPRESSED_LEN, -1, file->compressed_length);
}


static void
after_chunk_read (mongoc_gridfs_file_t *file, const uint8_t **data, uint32_t *len)
{
   mongoc_gridfs_cnv_file_t *cnv_file = (mongoc_gridfs_cnv_file_t *)file->chunk_callbacks_custom_data;

   if (cnv_file->flags & MONGOC_CNV_DECRYPT)
      eax_decrypt((uint8_t *)*data, *len, &cnv_file->aes_ctx);
   if (cnv_file->flags & MONGOC_CNV_UNCOMPRESS) {
      uint32_t uncompressed_len = file->chunk_size;

      uncompress (cnv_file->buf_for_compress_encrypt, &uncompressed_len, *data, *len);

      *data = cnv_file->buf_for_compress_encrypt;
      *len = uncompressed_len;
   } else if (*len > (uint32_t)file->chunk_size) {
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
      compress2 (cnv_file->buf_for_compress_encrypt, &compressed_len, *data, *len, Z_BEST_SPEED);
      cnv_file->compressed_length += compressed_len;

      *data = cnv_file->buf_for_compress_encrypt;
      *len = compressed_len;
   }
   if (cnv_file->flags & MONGOC_CNV_ENCRYPT) {
      memcpy (cnv_file->buf_for_compress_encrypt, *data, *len);
      eax_encrypt (cnv_file->buf_for_compress_encrypt, *len, &cnv_file->aes_ctx);
      *data = cnv_file->buf_for_compress_encrypt;
      cnv_file->is_encrypted = true;
   }
   if (cnv_file->need_to_append_metadata) {
      append_metadata (cnv_file);
      cnv_file->need_to_append_metadata = false;
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

   if (bson_iter_init_find (&it, metadata, MONGOC_CNV_GRIDFS_FILE_COMPRESSED_LEN))
      cnv_file->compressed_length = bson_iter_int64 (&it);
   if (!(cnv_file->flags & MONGOC_CNV_UNCOMPRESS) && mongoc_gridfs_cnv_file_is_compressed (cnv_file)) {
      /* need to set size to be actual size in case reading compressed file without uncompress
         cause implementation depends on this */
      cnv_file->length_fix = cnv_file->file->length - cnv_file->compressed_length;
      cnv_file->file->length = cnv_file->compressed_length;
   }
   if (bson_iter_init_find (&it, metadata, MONGOC_CNV_GRIDFS_FILE_AES_IV)) {
      const uint8_t *aes_initialization_vector;
      uint32_t len;
      bson_iter_binary (&it, NULL, &len, &aes_initialization_vector);
      BSON_ASSERT (len == sizeof cnv_file->aes_initialization_vector);
      memcpy (cnv_file->aes_initialization_vector, aes_initialization_vector, sizeof cnv_file->aes_initialization_vector);
   }
   if (bson_iter_init_find (&it, metadata, MONGOC_CNV_GRIDFS_FILE_AES_SALT)) {
      const uint8_t *aes_salt;
      uint32_t len;
      bson_iter_binary (&it, NULL, &len, &aes_salt);
      BSON_ASSERT (len == sizeof cnv_file->salt);
      memcpy (cnv_file->salt, aes_salt, sizeof cnv_file->salt);
   }
   cnv_file->is_encrypted = bson_iter_init_find (&it, metadata, MONGOC_CNV_GRIDFS_FILE_AES_TAG);
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
   cnv_file->need_to_append_metadata = false;
   if (cnv_file->flags & MONGOC_CNV_COMPRESS ||
       cnv_file->flags & MONGOC_CNV_UNCOMPRESS ||
       cnv_file->flags & MONGOC_CNV_ENCRYPT)
      cnv_file->buf_for_compress_encrypt = bson_malloc0 (compressBound (file->chunk_size));
   else
      cnv_file->buf_for_compress_encrypt = NULL;
   cnv_file->file->chunk_callbacks_custom_data = cnv_file;
   mongoc_gridfs_file_set_chunk_callbacks (file, &callbacks);

   cnv_file->aes_key_is_valid = false;
   cnv_file->is_encrypted = false;
   if (cnv_file->flags & MONGOC_CNV_ENCRYPT)
      fill_buf_with_rand (cnv_file->aes_initialization_vector, sizeof cnv_file->aes_initialization_vector);
   else
      memset (cnv_file->aes_initialization_vector, 0, sizeof cnv_file->aes_initialization_vector);

   load_metadata (cnv_file);

   if (cnv_file->flags & MONGOC_CNV_ENCRYPT || cnv_file->flags & MONGOC_CNV_DECRYPT)
      eax_init_message (cnv_file->aes_initialization_vector,  sizeof cnv_file->aes_initialization_vector,
                        &cnv_file->aes_ctx);

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

mongoc_gridfs_cnv_file_t *mongoc_gridfs_cnv_file_from_file (mongoc_gridfs_file_t           *file,
                                                            mongoc_gridfs_cnv_file_flags_t  flags)
{
   return mongoc_gridfs_cnv_file_new (file, flags);
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
      bson_free (file->buf_for_compress_encrypt);
   if(file->flags & MONGOC_CNV_ENCRYPT || file->flags & MONGOC_CNV_DECRYPT)
      eax_end (&file->aes_ctx);
   bson_free (file);
}

static bool
check_decrypted_data_integrity (mongoc_gridfs_cnv_file_t *file)
{
   unsigned char tag[AES_BLOCK_SIZE];
   const bson_t *metadata = mongoc_gridfs_cnv_file_get_metadata (file);
   bson_iter_t it;
   uint8_t *saved_after_encryption_tag;
   uint32_t saved_after_encryption_tag_len;

   bson_iter_init_find (&it, metadata, MONGOC_CNV_GRIDFS_FILE_AES_TAG);
   bson_iter_binary (&it, NULL, &saved_after_encryption_tag_len, &saved_after_encryption_tag);
   BSON_ASSERT (saved_after_encryption_tag_len == AES_BLOCK_SIZE);

   eax_compute_tag (tag, sizeof tag, &file->aes_ctx);

   return memcmp (tag, saved_after_encryption_tag, AES_BLOCK_SIZE) == 0;
}

ssize_t
mongoc_gridfs_cnv_file_readv (mongoc_gridfs_cnv_file_t *file,
                              mongoc_iovec_t           *iov,
                              size_t                    iovcnt,
                              size_t                    min_bytes,
                              uint32_t                  timeout_msec)
{
   ssize_t ret;

   if (file->file->failed = file->flags & MONGOC_CNV_DECRYPT && !file->aes_key_is_valid) {
      bson_set_error(&file->file->error, 0, 0, ERR_MSG_INVALID_AES_KEY);
      return -1;
   }

   ret = mongoc_gridfs_file_readv (file->file, iov, iovcnt, min_bytes, timeout_msec);
   if (ret > 0) {
      ret += file->read_length_fix;
      file->read_length_fix = 0;
   }

   if (file->flags & MONGOC_CNV_DECRYPT && ret == 0) {
      if (file->file->failed = !check_decrypted_data_integrity (file)) {
         bson_set_error(&file->file->error, 0, 0, ERR_MSG_AES_EAX_INTEGRITY_CHECK_FAILED);
         return -1;
      }
   }

   return ret;
}

ssize_t
mongoc_gridfs_cnv_file_writev (mongoc_gridfs_cnv_file_t *file,
                               mongoc_iovec_t           *iov,
                               size_t                    iovcnt,
                               uint32_t                  timeout_msec)
{
   if (file->file->failed = file->flags & MONGOC_CNV_ENCRYPT && !file->aes_key_is_valid) {
      bson_set_error(&file->file->error, 0, 0, ERR_MSG_INVALID_AES_KEY);
      return -1;
   }

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


bool
mongoc_gridfs_cnv_file_error (mongoc_gridfs_cnv_file_t *file,
                              bson_error_t             *error)
{
   return mongoc_gridfs_file_error (file->file, error);
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
   if (file->flags & MONGOC_CNV_COMPRESS || file->flags & MONGOC_CNV_ENCRYPT) {
      bson_t metadata = BSON_INITIALIZER;
      mongoc_gridfs_file_set_metadata (file->file, &metadata);

      if (file->file->page && _mongoc_gridfs_file_page_is_dirty (file->file->page))
         /* we can't set metadata here cause page is pending flush
            we have no final data at this point and we'll append metadata before chunk write */
         file->need_to_append_metadata = true;
      else
         append_metadata (file);
   }

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

bool
mongoc_gridfs_cnv_file_set_aes_key (mongoc_gridfs_cnv_file_t *file,
                                    const unsigned char      *aes_key,
                                    uint16_t                  aes_key_size)
{
   static const uint16_t AES_VALID_SIZE_256 = 256, 
                         AES_VALID_SIZE_192 = 192, 
                         AES_VALID_SIZE_128 = 128, 
                         BITS_IN_BYTE = 8;

   file->aes_key_is_valid = aes_key_size == AES_VALID_SIZE_256 / BITS_IN_BYTE || 
                            aes_key_size == AES_VALID_SIZE_192 / BITS_IN_BYTE || 
                            aes_key_size == AES_VALID_SIZE_128 / BITS_IN_BYTE;
   eax_init_and_key (aes_key, aes_key_size, &file->aes_ctx);

   return file->aes_key_is_valid;
}

bool
mongoc_gridfs_cnv_file_set_aes_key_from_password (mongoc_gridfs_cnv_file_t *file,
                                                  const char               *password,
                                                  uint16_t                  password_size)
{
   #define AUTOGEN_AES_KEY_SIZE 32
   uint8_t key[AUTOGEN_AES_KEY_SIZE];
   static const uint64_t SRCYPT_n_that_takes_avg_sec_to_generate_key = 4096;

   if (file->flags & MONGOC_CNV_ENCRYPT)
      libscrypt_salt_gen (file->salt, sizeof file->salt);
   libscrypt_scrypt (password, password_size, file->salt, sizeof file->salt, 
                     SRCYPT_n_that_takes_avg_sec_to_generate_key, SCRYPT_r, SCRYPT_p,
                     key, sizeof key);

   return mongoc_gridfs_cnv_file_set_aes_key (file, key, sizeof key);
}

bool
mongoc_gridfs_cnv_file_is_encrypted (mongoc_gridfs_cnv_file_t *file)
{
   return file->is_encrypted;
}
