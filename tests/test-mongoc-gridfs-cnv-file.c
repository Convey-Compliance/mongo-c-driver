#include "mongoc-tests.h"

#include <fcntl.h>
#include <mongoc.h>
#include <stdlib.h>
#include <mongoc-gridfs-cnv-file.h>
#include <zlib.h>

#include "test-libmongoc.h"
#include "TestSuite.h"


static char *gTestUri, *gDbName = "test";
static const char hello_world[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' },
                  compressed_hello_world[] = { 0x78, 0x01, 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 
                                               0x57, 0x28, 0xCF, 0x2F, 0xCA, 0x49, 0x01, 0x00, 
                                               0x1A, 0x0B, 0x04, 0x5D };
static const mongoc_iovec_t hello_world_iov = { sizeof hello_world, (char *)hello_world },
                            compressed_hello_world_iov = { sizeof compressed_hello_world, 
                                                           (char *)compressed_hello_world };

#define SETUP_DECLARATIONS(file_name) \
   mongoc_client_t *client; \
   mongoc_gridfs_t *gridfs; \
   bson_error_t error; \
   mongoc_gridfs_cnv_file_t *file; \
   const char *filename = file_name; \
   mongoc_gridfs_file_opt_t opt = { NULL, filename };

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
test_write_compressed (void)
{
   SETUP_DECLARATIONS ("test_write_compressed")
   char buf[4096];
   mongoc_iovec_t iov = { sizeof buf, buf };
   bson_iter_t it;

   SETUP

   /* white hello world using cnv stream with compression enabled */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_COMPRESS);
   assert (file);
   assert (mongoc_gridfs_cnv_file_writev (file, (mongoc_iovec_t*)&hello_world_iov, 1, 0) == sizeof hello_world);
   assert (mongoc_gridfs_cnv_file_save (file));
   mongoc_gridfs_cnv_file_destroy (file);

   /* read file without uncompress */
   file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_NONE);
   assert (file);

   /* check medatada is written */
   assert (mongoc_gridfs_cnv_file_get_metadata (file));
   assert (bson_iter_init_find (&it, mongoc_gridfs_cnv_file_get_metadata (file), "compressed_length"));
   assert (sizeof compressed_hello_world == bson_iter_int64 (&it));

   /* length should be same as compressed length */
   assert (mongoc_gridfs_cnv_file_is_compressed (file));
   assert (mongoc_gridfs_cnv_file_get_length (file) == sizeof compressed_hello_world);
   assert (mongoc_gridfs_cnv_file_get_compressed_length (file ) == sizeof compressed_hello_world);

   /* check written file is compressed */
   assert (mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0) == sizeof compressed_hello_world);
   assert (memcmp (compressed_hello_world_iov.iov_base, iov.iov_base, sizeof compressed_hello_world) == 0);

   mongoc_gridfs_cnv_file_destroy (file);
   TEARDOWN
}


static void
test_read_compressed (void)
{
   SETUP_DECLARATIONS ("test_read_compressed")
   char buf[4096];
   mongoc_iovec_t iov = { sizeof buf, buf };
   ssize_t r;

   SETUP

   /* write compressed hello world */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_COMPRESS);
   assert (mongoc_gridfs_cnv_file_writev (file, (mongoc_iovec_t*)&hello_world_iov, 1, 0) == sizeof hello_world);
   assert (mongoc_gridfs_cnv_file_save(file));
   mongoc_gridfs_cnv_file_destroy(file);

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
test_read_after_seek (void)
{
   SETUP_DECLARATIONS ("test_read_after_seek")
   char buf[4096];
   mongoc_iovec_t iov = { sizeof buf, buf };
   ssize_t r;
   uint64_t seek_delta = 5;

   SETUP

   /* write compressed hello world */
   file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_COMPRESS);
   assert (mongoc_gridfs_cnv_file_writev (file, (mongoc_iovec_t*)&hello_world_iov, 1, 0) == sizeof hello_world);
   assert (mongoc_gridfs_cnv_file_save(file));
   mongoc_gridfs_cnv_file_destroy(file);

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
test_write_read_10mb (void)
{
   SETUP_DECLARATIONS ("test_write_read_10mb")
   const size_t DATA_LEN = 10 * 1024 * 1024;
   char *data_buf = bson_malloc0 (DATA_LEN), 
        *compressed_buf, 
        *uncompressed_buf = bson_malloc0 (DATA_LEN), 
         buf[4096];
   mongoc_iovec_t iov;
   int64_t i, expected_compressed_len, compressed_len;

   SETUP

   /* write random data to gridfs file */
   srand ((unsigned int)time (NULL));
   for (i = 0; i < DATA_LEN; ++i)
      data_buf[i] = rand() % 256;

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
   assert (mongoc_gridfs_cnv_file_get_length (file) == compressed_len);
   compressed_buf = bson_malloc0 ((size_t)compressed_len);
   iov.iov_len = (size_t)compressed_len;
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
   bson_free (uncompressed_buf);
   TEARDOWN
}


static void
cleanup_globals (void)
{
   bson_free (gTestUri);
}


void
test_gridfs_cnv_file_install (TestSuite *suite)
{
   gTestUri = bson_strdup_printf ("mongodb://%s/", MONGOC_TEST_HOST);

   TestSuite_Add (suite, "/Cnv_file/write/compressed", test_write_compressed);
   TestSuite_Add (suite, "/Cnv_file/read/compressed", test_read_compressed);
   TestSuite_Add (suite, "/Cnv_file/read/after_seek", test_read_after_seek);
   TestSuite_Add (suite, "/Cnv_file/write_read/10mb", test_write_read_10mb);

   atexit (cleanup_globals);
}
