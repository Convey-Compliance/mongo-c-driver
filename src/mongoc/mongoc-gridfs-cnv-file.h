#if !defined (MONGOC_INSIDE) && !defined (MONGOC_COMPILATION)
#error "Only <mongoc.h> can be included directly."
#endif


#ifndef MONGOC_GRIDFS_CNV_FILE_H
#define MONGOC_GRIDFS_CNV_FILE_H


#include "mongoc-gridfs-file.h"
#include "mongoc-gridfs.h"
#include <eax.h>
#include <libscrypt.h>


BSON_BEGIN_DECLS


typedef enum
{
   MONGOC_CNV_NONE        = 0,
   MONGOC_CNV_COMPRESS    = 1 << 1,
   MONGOC_CNV_ENCRYPT     = 1 << 2,
   MONGOC_CNV_UNCOMPRESS  = 1 << 3,
   MONGOC_CNV_DECRYPT     = 1 << 4
} mongoc_gridfs_cnv_file_flags_t;


typedef struct _mongoc_gridfs_cnv_file_t mongoc_gridfs_cnv_file_t;


struct _mongoc_gridfs_cnv_file_t
{
  mongoc_gridfs_file_t          *file;
  mongoc_gridfs_cnv_file_flags_t flags;
  uint8_t                       *buf_for_compress;
  int64_t                        compressed_length;
  uint32_t                       length_fix;
  uint32_t                       read_length_fix;
  bool                           need_to_append_metadata;
  eax_ctx                        aes_ctx;
  bool                           aes_key_is_valid;
  bool                           is_encrypted;
  uint8_t                        aes_initialization_vector[AES_BLOCK_SIZE];
  uint8_t                        salt[SCRYPT_SALT_LEN];
};


mongoc_gridfs_cnv_file_t *mongoc_gridfs_create_cnv_file_from_stream (mongoc_gridfs_t                *gridfs,
                                                                     mongoc_stream_t                *stream,
                                                                     mongoc_gridfs_file_opt_t       *opt,
                                                                     mongoc_gridfs_cnv_file_flags_t  flags);
mongoc_gridfs_cnv_file_t *mongoc_gridfs_create_cnv_file             (mongoc_gridfs_t                *gridfs,
                                                                     mongoc_gridfs_file_opt_t       *opt,
                                                                     mongoc_gridfs_cnv_file_flags_t  flags);
mongoc_gridfs_cnv_file_t *mongoc_gridfs_find_one_cnv                (mongoc_gridfs_t                *gridfs,
                                                                     const bson_t                   *query,
                                                                     bson_error_t                   *error,
                                                                     mongoc_gridfs_cnv_file_flags_t  flags);
mongoc_gridfs_cnv_file_t *mongoc_gridfs_find_one_cnv_by_filename    (mongoc_gridfs_t                *gridfs,
                                                                     const char                     *filename,
                                                                     bson_error_t                   *error,
                                                                     mongoc_gridfs_cnv_file_flags_t  flags);

#define MONGOC_GRIDFS_CNV_FILE_STR_HEADER(name) \
   const char * \
   mongoc_gridfs_cnv_file_get_##name (mongoc_gridfs_cnv_file_t *file); \
   void \
   mongoc_gridfs_cnv_file_set_##name (mongoc_gridfs_cnv_file_t *file, \
                                      const char               *str);


#define MONGOC_GRIDFS_CNV_FILE_BSON_HEADER(name) \
   const bson_t * \
   mongoc_gridfs_cnv_file_get_##name (mongoc_gridfs_cnv_file_t *file); \
   void \
   mongoc_gridfs_cnv_file_set_##name (mongoc_gridfs_cnv_file_t *file, \
                                      const bson_t             *bson);

MONGOC_GRIDFS_CNV_FILE_STR_HEADER (md5)
MONGOC_GRIDFS_CNV_FILE_STR_HEADER (filename)
MONGOC_GRIDFS_CNV_FILE_STR_HEADER (content_type)
MONGOC_GRIDFS_CNV_FILE_BSON_HEADER (aliases)
MONGOC_GRIDFS_CNV_FILE_BSON_HEADER (metadata)


bool     mongoc_gridfs_cnv_file_error           (mongoc_gridfs_cnv_file_t *file,
                                                 bson_error_t             *error);
int64_t  mongoc_gridfs_cnv_file_get_length      (mongoc_gridfs_cnv_file_t *file);
int32_t  mongoc_gridfs_cnv_file_get_chunk_size  (mongoc_gridfs_cnv_file_t *file);
int64_t  mongoc_gridfs_cnv_file_get_upload_date (mongoc_gridfs_cnv_file_t *file);
ssize_t  mongoc_gridfs_cnv_file_writev          (mongoc_gridfs_cnv_file_t *file,
                                                 mongoc_iovec_t           *iov,
                                                 size_t                    iovcnt,
                                                 uint32_t                  timeout_msec);
ssize_t  mongoc_gridfs_cnv_file_readv           (mongoc_gridfs_cnv_file_t *file,
                                                 mongoc_iovec_t           *iov,
                                                 size_t                    iovcnt,
                                                 size_t                    min_bytes,
                                                 uint32_t                  timeout_msec);
int      mongoc_gridfs_cnv_file_seek            (mongoc_gridfs_cnv_file_t *file,
                                                 uint64_t                  delta,
                                                 int                       whence);
uint64_t mongoc_gridfs_cnv_file_tell            (mongoc_gridfs_cnv_file_t *file);
bool     mongoc_gridfs_cnv_file_save            (mongoc_gridfs_cnv_file_t *file);
void     mongoc_gridfs_cnv_file_destroy         (mongoc_gridfs_cnv_file_t *file);
bool     mongoc_gridfs_cnv_file_error           (mongoc_gridfs_cnv_file_t *file,
                                                 bson_error_t             *error);
bool     mongoc_gridfs_cnv_file_remove          (mongoc_gridfs_cnv_file_t *file,
                                                 bson_error_t             *error);
bool    mongoc_gridfs_cnv_file_is_compressed    (mongoc_gridfs_cnv_file_t *file);
int64_t mongoc_gridfs_cnv_file_get_compressed_length      (mongoc_gridfs_cnv_file_t *file);
bool    mongoc_gridfs_cnv_file_set_aes_key      (mongoc_gridfs_cnv_file_t *file,
                                                 const unsigned char      *aes_key,
                                                 uint16_t                  aes_key_size);
bool    mongoc_gridfs_cnv_file_set_aes_key_from_password  (mongoc_gridfs_cnv_file_t *file,
                                                           const char               *password,
                                                           uint16_t                  password_size);
bool    mongoc_gridfs_cnv_file_is_encrypted     (mongoc_gridfs_cnv_file_t *file);


BSON_END_DECLS


#endif /* MONGOC_GRIDFS_CNV_FILE_H */
