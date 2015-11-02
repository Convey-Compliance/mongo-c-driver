#include "mongoc-tests.h"

#include <fcntl.h>
#include <mongoc.h>
#include <stdlib.h>
#include <mongoc-gridfs-cnv-file.h>
#include <zlib.h>
#include <eax.h>

#include "test-libmongoc.h"
#include "TestSuite.h"


char *gTestUri, *gDbName = "test";
const char hello_world[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' },
           compressed_hello_world[] = { 0x78, 0x01, 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 
                                        0x57, 0x28, 0xCF, 0x2F, 0xCA, 0x49, 0x01, 0x00, 
                                        0x1A, 0x0B, 0x04, 0x5D };
const mongoc_iovec_t hello_world_iov = { sizeof hello_world, (char *)hello_world },
                     compressed_hello_world_iov = { sizeof compressed_hello_world, 
                                                    (char *)compressed_hello_world };
const unsigned char aes_key[] = { 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 
                                  0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A };

#define SETUP_DECLARATIONS(file_name) \
   mongoc_client_t *client; \
   mongoc_gridfs_t *gridfs; \
   bson_error_t error; \
   mongoc_gridfs_cnv_file_t *file; \
   const char *filename = file_name; \
   mongoc_gridfs_file_opt_t opt = { NULL, filename }; \
   char buf[4096]; \
   mongoc_iovec_t iov = { sizeof buf, buf };

#define SETUP \
   client = mongoc_client_new (gTestUri); \
   assert (client); \
   \
   gridfs = mongoc_client_get_gridfs (client, gDbName, filename, &error); \
   assert (gridfs); \
   \
   mongoc_gridfs_drop (gridfs, &error);

#define TEARDOWN \
   mongoc_gridfs_destroy (gridfs); \
   mongoc_client_destroy (client);


static void
write_hello_world_to_file (mongoc_gridfs_cnv_file_t *file)
{
   assert(file);

   assert (mongoc_gridfs_cnv_file_writev (file, (mongoc_iovec_t*)&hello_world_iov, 1, 0) == sizeof hello_world);
   assert (mongoc_gridfs_cnv_file_save(file));
   mongoc_gridfs_cnv_file_destroy(file);
}


static void
fill_buf_with_rand_data (char *buf, size_t len)
{
   size_t i;

   srand ((unsigned int)time (NULL));
   for (i = 0; i < len; ++i)
      buf[i] = rand() % 256;
}


static void
test_compressed_write (void)
{
   SETUP_DECLARATIONS ("test_compressed_write")
   bson_iter_t it;

   SETUP

   /* white hello world with compression enabled */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_COMPRESS);
   write_hello_world_to_file (file);

   /* read file without uncompress */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_NONE);
   assert (file);

   /* check medatada is written */
   assert (mongoc_gridfs_cnv_file_get_metadata (file));
   assert (bson_iter_init_find (&it, mongoc_gridfs_cnv_file_get_metadata (file), "compressed_len"));
   assert (sizeof compressed_hello_world == bson_iter_int64 (&it));

   /* check properties */
   assert (mongoc_gridfs_cnv_file_is_compressed (file));
   assert (!mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_get_length (file) == sizeof hello_world);
   assert (mongoc_gridfs_cnv_file_get_compressed_length (file) == sizeof compressed_hello_world);

   /* check written file is compressed */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == sizeof compressed_hello_world);
   assert (memcmp (compressed_hello_world_iov.iov_base, iov.iov_base, sizeof compressed_hello_world) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_compressed_read (void)
{
   SETUP_DECLARATIONS ("test_compressed_read")
   ssize_t r;

   SETUP

   /* write compressed hello world */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_COMPRESS);
   write_hello_world_to_file (file);

   /* read compressed file and uncompress it */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_UNCOMPRESS);
   assert(file);

   /* length should be same as not compressed length
      use get_compressed_length to know compressed length */
   assert (mongoc_gridfs_cnv_file_is_compressed (file));
   assert (mongoc_gridfs_cnv_file_get_length (file) == sizeof hello_world);
   assert (mongoc_gridfs_cnv_file_get_compressed_length (file ) == sizeof compressed_hello_world);

   r = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);

   /* check readed file is uncompressed */
   assert (sizeof hello_world == r);
   assert (memcmp (hello_world, iov.iov_base, r) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_compressed_read_after_seek (void)
{
   SETUP_DECLARATIONS ("test_read_after_seek")
   ssize_t r;
   uint64_t seek_delta = 5;

   SETUP

   /* write compressed hello world */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_COMPRESS);
   write_hello_world_to_file (file);

   /* read compressed file and uncompress it */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_UNCOMPRESS);
   assert(file);

   /* seek on few bytes */
   assert (mongoc_gridfs_cnv_file_seek (file, seek_delta, SEEK_SET) == 0);
   r = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);

   /* check readed data after seek 
      seek should be based on uncompressed data, not on compressed */
   assert (r == sizeof hello_world - seek_delta);
   assert (memcmp (hello_world + seek_delta, iov.iov_base, r) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_compressed_write_read_10mb (void)
{
   SETUP_DECLARATIONS ("test_compressed_write_read_10mb")
   const size_t DATA_LEN = 10 * 1024 * 1024;
   size_t i;
   char *data_buf = bson_malloc0 (DATA_LEN), 
        *compressed_buf;
   int64_t expected_compressed_len, compressed_len;

   SETUP
   fill_buf_with_rand_data (data_buf, DATA_LEN);

   /* write random data to gridfs file */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_COMPRESS);
   iov.iov_len = sizeof buf;
   for (i = 0; i < DATA_LEN; i += sizeof buf) {
      iov.iov_base = data_buf + i;
      assert (mongoc_gridfs_cnv_file_writev (file, &iov, 1, 0) >= 0);
   }
   assert (mongoc_gridfs_cnv_file_save (file));
   expected_compressed_len = mongoc_gridfs_cnv_file_get_compressed_length (file);
   mongoc_gridfs_cnv_file_destroy (file);

   /* get compressed written data
      this is needed to test behavior while chunk overhead occurs see comments in after_chunk_read implementation */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_NONE);
   assert (file);

   assert (mongoc_gridfs_cnv_file_is_compressed (file));
   compressed_len = mongoc_gridfs_cnv_file_get_compressed_length (file);
   assert (compressed_len == expected_compressed_len);
   assert (mongoc_gridfs_cnv_file_get_length (file) == DATA_LEN);
   compressed_buf = bson_malloc0 ((size_t)compressed_len);
   iov.iov_len = (u_long)compressed_len;
   iov.iov_base = compressed_buf;
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == compressed_len);
   mongoc_gridfs_cnv_file_destroy (file);

   /* test read compressed data using buf */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_UNCOMPRESS);
   assert(file);

   i = 0;
   iov.iov_base = buf;
   while (1) {
      ssize_t nread;
      iov.iov_len = sizeof buf;
      nread = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);
      assert (nread >= 0);
      if (nread == 0)
         break;
      assert (memcmp (data_buf + i, buf, nread) == 0);
      i += nread;
   }
   assert (DATA_LEN == i);

   /* teardown */
   mongoc_gridfs_cnv_file_destroy (file);
   bson_free (data_buf);
   bson_free (compressed_buf);
   TEARDOWN
}


static void
test_encrypted_read_write_without_aes_key (void)
{
   SETUP_DECLARATIONS ("test_write_encrypted_without_aes_key")
   bson_error_t err;

   SETUP

   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT);
   /* notice we didn't call mongoc_gridfs_cnv_file_set_aes_key, but we passed MONGOC_CNV_ENCRYPT flag */
   assert (file);

   assert (mongoc_gridfs_cnv_file_writev (file, (mongoc_iovec_t*)&hello_world_iov, 1, 0) == -1);
   assert (mongoc_gridfs_cnv_file_error (file, &err));
   assert (strcmp (err.message, "Invalid AES key") == 0);

   mongoc_gridfs_cnv_file_destroy (file);

   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_DECRYPT);
   /* same when try to read and decrypt file without set aes key */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == -1);
   assert (mongoc_gridfs_cnv_file_error (file, &err));
   assert (strcmp (err.message, "Invalid AES key") == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_encrypted_write (void)
{
   SETUP_DECLARATIONS ("test_encrypted_write")
   bson_iter_t it;

   SETUP

   /* white hello world with encryption enabled */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));
   write_hello_world_to_file (file);

   /* read file without decryption */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_NONE);
   assert (file);

   /* check medatada is written */
   assert (mongoc_gridfs_cnv_file_get_metadata (file));
   assert (bson_iter_init_find (&it, mongoc_gridfs_cnv_file_get_metadata (file), "aes_initialization_vector"));
   assert (bson_iter_init_find (&it, mongoc_gridfs_cnv_file_get_metadata (file), "aes_tag"));

   /* should be encrypted */
   assert (!mongoc_gridfs_cnv_file_is_compressed (file));
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_get_compressed_length (file ) == 0);

   /* check written file is encrypted */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == sizeof hello_world);
   assert (memcmp (hello_world, iov.iov_base, sizeof hello_world) != 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_encrypted_read (void)
{
   SETUP_DECLARATIONS ("test_encrypted_read")
   ssize_t r;

   SETUP

   /* write encrypted hello world */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));
   write_hello_world_to_file (file);

   /* read encrypted file and decrypt it */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_DECRYPT);
   assert(file);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   /* should be encrypted */
   assert (!mongoc_gridfs_cnv_file_is_compressed (file));
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_get_compressed_length (file ) == 0);

   r = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);
   /* call second time to force integrity check using aes eax tag */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == 0);

   /* check readed file is decrypted */
   assert (sizeof hello_world == r);
   assert (memcmp (hello_world, iov.iov_base, r) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_encrypted_read_after_seek (void)
{
   SETUP_DECLARATIONS ("test_encrypted_read_after_seek")
   ssize_t r;
   int64_t seek_delta = -5;

   SETUP

   /* write encrypted hello world */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));
   write_hello_world_to_file (file);

   /* read encrypted file and decrypt it */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_DECRYPT);
   assert(file);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   /* seek on few bytes */
   assert (mongoc_gridfs_cnv_file_seek (file, seek_delta, SEEK_END) == 0);
   r = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);

   /* check readed data after seek 
      seek should be based on decrypted data */
   assert (r == 5);
   assert (memcmp (hello_world + sizeof hello_world - r, iov.iov_base, r) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_encrypted_write_read_10mb (void)
{
   SETUP_DECLARATIONS ("test_encrypted_write_read_10mb")
   const size_t DATA_LEN = 10 * 1024 * 1024;
   size_t i;
   char *data_buf = bson_malloc0 (DATA_LEN), 
        *encrypted_buf = bson_malloc0 (DATA_LEN);

   SETUP
   fill_buf_with_rand_data (data_buf, DATA_LEN);

   /* write random data to gridfs file */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT);
   assert (file);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   iov.iov_len = sizeof buf;
   for (i = 0; i < DATA_LEN; i += sizeof buf) {
      iov.iov_base = data_buf + i;
      assert (mongoc_gridfs_cnv_file_writev (file, &iov, 1, 0) >= 0);
   }
   assert (mongoc_gridfs_cnv_file_save (file));
   mongoc_gridfs_cnv_file_destroy (file);

   /* get encrypted written data without decryption */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_NONE);
   assert (file);

   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   iov.iov_len = DATA_LEN;
   iov.iov_base = encrypted_buf;
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == DATA_LEN);
   mongoc_gridfs_cnv_file_destroy (file);

   /* check that data is encrypted */
   assert (memcmp (data_buf, encrypted_buf, DATA_LEN) != 0);

   /* read encrypted data with decryption */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_DECRYPT);
   assert (file);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   i = 0;
   iov.iov_base = buf;
   while (1) {
      ssize_t nread;
      iov.iov_len = sizeof buf;
      nread = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);
      assert (nread >= 0);
      if (nread == 0)
         break;
      assert (memcmp (data_buf + i, buf, nread) == 0);
      i += nread;
   }
   assert (DATA_LEN == i);

   /* teardown */
   mongoc_gridfs_cnv_file_destroy (file);
   bson_free (data_buf);
   bson_free (encrypted_buf);
   TEARDOWN
}


static void
write_corrupted_file_stub (mongoc_gridfs_cnv_file_t *file)
{
   char garbage_buf[20], garbage_tag[AES_BLOCK_SIZE], garbage_iv[AES_BLOCK_SIZE];
   bson_t metadata = BSON_INITIALIZER;
   mongoc_iovec_t iov = { sizeof garbage_buf, garbage_buf };

   fill_buf_with_rand_data (garbage_buf, sizeof garbage_buf);
   fill_buf_with_rand_data (garbage_tag, sizeof garbage_tag);
   fill_buf_with_rand_data (garbage_iv, sizeof garbage_iv);
   /* append initialization vector and tag as we do while encrypting file */
   assert (bson_append_binary (&metadata, "aes_initialization_vector", -1, BSON_SUBTYPE_BINARY, garbage_iv, sizeof garbage_iv));
   assert (bson_append_binary (&metadata, "aes_tag", -1, BSON_SUBTYPE_BINARY, garbage_tag, sizeof garbage_tag));

   assert (mongoc_gridfs_cnv_file_writev (file, &iov, 1, 0) >= 0);
   mongoc_gridfs_cnv_file_set_metadata (file, &metadata);
   assert (mongoc_gridfs_cnv_file_save (file));
   mongoc_gridfs_cnv_file_destroy (file);
}


static void
test_encrypted_integrity_check_failed (void)
{
   SETUP_DECLARATIONS ("test_encrypted_integrity_check_failed")
   bson_error_t err;

   SETUP

   /* write garbage to file to imitate corrupted data */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_NONE);
   assert(file);
   write_corrupted_file_stub (file);

   /* read encrypted file and decrypt it */
   file = mongoc_gridfs_find_one_cnv_by_filename (gridfs, filename, &error, MONGOC_CNV_DECRYPT);
   assert(file);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   /* should be encrypted */
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));

   /* read all data at first call */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) > 0);
   /* call second time to force integrity check using aes eax tag */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == -1);
   assert (mongoc_gridfs_cnv_file_error (file, &err));
   assert (strcmp (err.message, "Encrypted or decrypted data is corrupted or wrong key\\password used") == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_encrypted_write_using_password (void)
{
   SETUP_DECLARATIONS ("test_encrypted_write_using_password")
   const char pass[] = "111111";

   SETUP

   /* white hello world with encryption enabled using password instead aes key */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT);
   assert (mongoc_gridfs_cnv_file_set_aes_key_from_password (file, pass, sizeof pass));
   write_hello_world_to_file (file);

   /* get encrypted written data without decryption */
   file = mongoc_gridfs_find_one_cnv_by_filename (gridfs, filename, &error, MONGOC_CNV_NONE);
   assert (file);
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == sizeof hello_world);
   assert (memcmp (hello_world, iov.iov_base, sizeof hello_world) != 0);
   mongoc_gridfs_cnv_file_destroy (file);

   /* read encrypted data with decryption */
   file = mongoc_gridfs_find_one_cnv_by_filename (gridfs, filename, &error, MONGOC_CNV_DECRYPT);
   assert (file);
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_set_aes_key_from_password (file, pass, sizeof pass));

   /* check data is decrypted */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == sizeof hello_world);
   assert (memcmp (hello_world, iov.iov_base, sizeof hello_world) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_encrypted_write_using_password_read_using_key (void)
{
   SETUP_DECLARATIONS ("test_encrypted_write_using_password_read_using_key")
   const char pass[] = "2222222";
   bson_error_t err;

   SETUP

   /* white hello world with encryption enabled using password instead aes key */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT);
   assert (mongoc_gridfs_cnv_file_set_aes_key_from_password (file, pass, sizeof pass));
   write_hello_world_to_file (file);

   /* read encrypted data with decryption */
   file = mongoc_gridfs_find_one_cnv_by_filename (gridfs, filename, &error, MONGOC_CNV_DECRYPT);
   assert (file);
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   /* notice we use key here instead of password */
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   /* we should get integrity check error cause we used key instead password */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == sizeof hello_world);
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == -1);
   assert (mongoc_gridfs_cnv_file_error (file, &err));
   assert (strcmp (err.message, "Encrypted or decrypted data is corrupted or wrong key\\password used") == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_compressed_and_encrypted_write_read (void)
{
   SETUP_DECLARATIONS ("test_compressed_and_encrypted_write_read")

   SETUP

   /* white hello world with encryption and compression enabled */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT | MONGOC_CNV_COMPRESS);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));
   write_hello_world_to_file (file);

   /* read encrypted and compressed data */
   file = mongoc_gridfs_find_one_cnv_by_filename (gridfs, filename, &error, MONGOC_CNV_DECRYPT | MONGOC_CNV_UNCOMPRESS);
   assert (file);
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_is_compressed (file));
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   /* should match original */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == sizeof hello_world);
   assert (memcmp (hello_world, iov.iov_base, sizeof hello_world) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_compressed_and_encrypted_read_after_seek (void)
{
   SETUP_DECLARATIONS ("test_compressed_and_encrypted_read_after_seek")
   const char pass[] = "12345678";
   int64_t seek_delta = 3;
   ssize_t nread;

   SETUP

   /* white hello world with encryption and compression enabled */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT | MONGOC_CNV_COMPRESS);
   assert (mongoc_gridfs_cnv_file_set_aes_key_from_password (file, pass, sizeof pass));
   write_hello_world_to_file (file);

   /* read encrypted and compressed data */
   file = mongoc_gridfs_find_one_cnv_by_filename (gridfs, filename, &error, MONGOC_CNV_DECRYPT | MONGOC_CNV_UNCOMPRESS);
   assert (file);
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_is_compressed (file));
   assert (mongoc_gridfs_cnv_file_set_aes_key_from_password (file, pass, sizeof pass));

   /* should match original with offset */
   assert (mongoc_gridfs_cnv_file_seek (file, seek_delta, SEEK_CUR) == 0);
   assert (mongoc_gridfs_cnv_file_seek (file, seek_delta, SEEK_CUR) == 0);
   
   nread = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);
   assert (nread == sizeof hello_world - seek_delta * 2);
   assert (memcmp (hello_world + seek_delta * 2, iov.iov_base, nread) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_compressed_and_encrypted_write_read_10mb (void)
{
   SETUP_DECLARATIONS ("test_compressed_and_encrypted_write_read_10mb")
   const size_t DATA_LEN = 10 * 1024 * 1024;
   size_t i;
   char *data_buf = bson_malloc0 (DATA_LEN);

   SETUP
   fill_buf_with_rand_data (data_buf, DATA_LEN);

   /* white data with encryption and compression enabled */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT | MONGOC_CNV_COMPRESS);
   assert (file);
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   iov.iov_len = sizeof buf;
   for (i = 0; i < DATA_LEN; i += sizeof buf) {
      iov.iov_base = data_buf + i;
      assert (mongoc_gridfs_cnv_file_writev (file, &iov, 1, 0) >= 0);
   }

   assert (mongoc_gridfs_cnv_file_save (file));
   mongoc_gridfs_cnv_file_destroy (file);

   /* read encrypted and compressed data */
   file = mongoc_gridfs_find_one_cnv_by_filename (gridfs, filename, &error, MONGOC_CNV_DECRYPT | MONGOC_CNV_UNCOMPRESS);
   assert (file);
   assert (mongoc_gridfs_cnv_file_is_encrypted (file));
   assert (mongoc_gridfs_cnv_file_is_compressed (file));
   assert (mongoc_gridfs_cnv_file_set_aes_key (file, aes_key, sizeof aes_key));

   i = 0;
   iov.iov_base = buf;
   while (1) {
      ssize_t nread;
      iov.iov_len = sizeof buf;
      nread = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);
      assert (nread >= 0);
      if (nread == 0)
         break;
      /* should match original */
      assert (memcmp (data_buf + i, buf, nread) == 0);
      i += nread;
   }
   assert (DATA_LEN == i);

   mongoc_gridfs_cnv_file_destroy (file);
   bson_free (data_buf);
   TEARDOWN
}


void
test_gridfs_cnv_file_install (TestSuite *suite)
{
   TestSuite_Add (suite, "/Cnv_file/compressed/write", test_compressed_write);
   TestSuite_Add (suite, "/Cnv_file/compressed/read", test_compressed_read);
   TestSuite_Add (suite, "/Cnv_file/compressed/read_after_seek", test_compressed_read_after_seek);
   TestSuite_Add (suite, "/Cnv_file/compressed/write_read_10mb", test_compressed_write_read_10mb);

   TestSuite_Add (suite, "/Cnv_file/encrypted/write_read_without_aes_key", test_encrypted_read_write_without_aes_key);
   TestSuite_Add (suite, "/Cnv_file/encrypted/write", test_encrypted_write);
   TestSuite_Add (suite, "/Cnv_file/encrypted/read", test_encrypted_read);
   TestSuite_Add (suite, "/Cnv_file/encrypted/read_after_seek", test_encrypted_read_after_seek);
   TestSuite_Add (suite, "/Cnv_file/encrypted/write_read_10mb", test_encrypted_write_read_10mb);
   TestSuite_Add (suite, "/Cnv_file/encrypted/integrity_check_failed", test_encrypted_integrity_check_failed);
   TestSuite_Add (suite, "/Cnv_file/encrypted/write_using_password", test_encrypted_write_using_password);
   TestSuite_Add (suite, "/Cnv_file/encrypted/write_using_password_read_using_key", test_encrypted_write_using_password_read_using_key);

   TestSuite_Add (suite, "/Cnv_file/compressed_and_encrypted/write_read", test_compressed_and_encrypted_write_read);
   TestSuite_Add (suite, "/Cnv_file/compressed_and_encrypted/read_after_seek", test_compressed_and_encrypted_read_after_seek);
   TestSuite_Add (suite, "/Cnv_file/compressed_and_encrypted/write_read_10mb", test_compressed_and_encrypted_write_read_10mb);
}
