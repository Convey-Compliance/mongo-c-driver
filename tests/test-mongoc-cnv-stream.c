#include "mongoc-tests.h"

#include <fcntl.h>
#include <mongoc.h>
#include <stdlib.h>
#include <mongoc-stream-cnv-gridfs.h>
#include <zlib.h>

#include "test-libmongoc.h"
#include "TestSuite.h"


static char *gTestUri, *gDbName = "test";
static const char compressed_hello_world[] = { 0x78, 0x01, 0xCA, 0x48, 0xCD, 0xC9, 0xC9, 
                                               0x57, 0x28, 0xCF, 0x2F, 0xCA, 0x49, 0x01, 0x00, 
                                               0x00, 0x00, 0xff, 0xff, /* Z_SYNC_FLUSH mark */
                                               0x03, 0x00, 0x1A, 0x0b, 0x04, 0x5d }; /* 6 bytes of Z_FINISH */
static const mongoc_iovec_t compressed_hello_world_iov = { sizeof compressed_hello_world, 
                                                           (char *)compressed_hello_world };


static void
test_write_compressed (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_gridfs_file_opt_t opt = { 0 };
   mongoc_client_t *client;
   mongoc_stream_t *stream;
   const char *filename = "test_write_compressed";
   ssize_t r, uncompressed_len;
   bson_error_t error;
   char buf[4096], uncompressed_buf[sizeof compressed_hello_world];
   mongoc_iovec_t iov = { 11, "hello world" }, compressed = { sizeof buf, buf };

   /* setup */
   client = mongoc_client_new (gTestUri);
   assert(client);

   gridfs = mongoc_client_get_gridfs (client, gDbName, filename, &error);
   assert(gridfs);
   mongoc_gridfs_drop (gridfs, &error);

   /* white hello world using cnv stream with compression enabled */
   opt.filename = filename;

   file = mongoc_gridfs_create_file (gridfs, &opt);
   stream = mongoc_stream_cnv_gridfs_new (file, MONGOC_CNV_COMPRESS);

   r = mongoc_stream_writev (stream, &iov, 1, 0);
   mongoc_stream_destroy (stream);
   mongoc_gridfs_file_destroy(file);

   /* check written file is compressed */
   file = mongoc_gridfs_find_one_by_filename(gridfs, filename, &error);
   assert (file);
   assert (mongoc_gridfs_file_readv (file, &compressed, 1, -1, 0) == sizeof compressed_hello_world);
   assert (memcmp (compressed_hello_world_iov.iov_base, compressed.iov_base, sizeof compressed_hello_world) == 0);

   /* uncompress and ensure it's hello world  */
   uncompressed_len = sizeof uncompressed_buf;
   assert (Z_OK == uncompress (uncompressed_buf, &uncompressed_len, compressed.iov_base, compressed.iov_len));
   assert (11 == uncompressed_len);
   assert (strncmp ("hello world", uncompressed_buf, uncompressed_len) == 0);

   /* teardown */
   mongoc_gridfs_file_destroy (file);
   mongoc_gridfs_destroy (gridfs);
   mongoc_client_destroy (client);
}


static void
test_read_compressed (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_gridfs_file_opt_t opt = { 0 };
   mongoc_client_t *client;
   mongoc_stream_t *stream;
   const char *filename = "test_read_compressed";
   bson_error_t error;
   char buf[4096];
   ssize_t r;
   mongoc_iovec_t compressed = { sizeof buf, buf };

   /* setup */
   client = mongoc_client_new (gTestUri);
   assert(client);

   gridfs = mongoc_client_get_gridfs (client, gDbName, filename, &error);
   assert(gridfs);
   mongoc_gridfs_drop (gridfs, &error);

   /* write compressed hello world stub */
   opt.filename = filename;

   file = mongoc_gridfs_create_file (gridfs, &opt);
   assert (mongoc_gridfs_file_writev (file, (mongoc_iovec_t*)&compressed_hello_world_iov, 1, 0) == sizeof compressed_hello_world);
   assert (mongoc_gridfs_file_save(file));
   mongoc_gridfs_file_destroy(file);

   /* read compressed hello world using cnv stream with compression enabled so it should decompress it */
   file = mongoc_gridfs_find_one_by_filename(gridfs, filename, &error);
   assert(file);

   stream = mongoc_stream_cnv_gridfs_new (file, MONGOC_CNV_COMPRESS);
   assert(stream);

   r = mongoc_stream_readv (stream, &compressed, 1, -1, 0);
   mongoc_stream_destroy (stream);

   /* check readed file is uncompressed */
   assert (11 == r);
   assert (memcmp ("hello world", compressed.iov_base, r) == 0);

   /* teardown */
   mongoc_gridfs_file_destroy (file);
   mongoc_gridfs_destroy (gridfs);
   mongoc_client_destroy (client);
}


static void
test_write_10mb (void)
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_gridfs_file_opt_t opt = { 0 };
   mongoc_client_t *client;
   mongoc_stream_t *stream;
   const char *filename = "test_write_10mb";
   bson_error_t error;
   const size_t DATA_LEN = 10 * 1024 * 1024;
   unsigned char *data_buf = bson_malloc0 (DATA_LEN), *compressed_buf, *uncompressed_buf, buf[4096];
   mongoc_iovec_t iov;
   size_t i, compressed_len, uncompressed_len;

   /* setup */
   client = mongoc_client_new (gTestUri);
   assert(client);

   gridfs = mongoc_client_get_gridfs (client, gDbName, filename, &error);
   assert(gridfs);
   mongoc_gridfs_drop (gridfs, &error);

   /* write random data to gridfs file */
   opt.filename = filename;

   srand ((unsigned int)time (NULL));
   for (i = 0; i < DATA_LEN; ++i)
      data_buf[i] = rand() % 256;

   file = mongoc_gridfs_create_file (gridfs, &opt);
   stream = mongoc_stream_cnv_gridfs_new (file, MONGOC_CNV_COMPRESS);

   iov.iov_len = sizeof buf;
   for (i = 0; i < DATA_LEN; i += sizeof buf) {
      iov.iov_base = data_buf + i;
      assert(mongoc_stream_writev (stream, &iov, 1, 0) >= 0);
   }
   mongoc_stream_destroy (stream);
   mongoc_gridfs_file_destroy(file);

   /* get compressed written data */
   file = mongoc_gridfs_find_one_by_filename(gridfs, filename, &error);
   assert(file);

   compressed_len = (size_t)mongoc_gridfs_file_get_length (file);
   compressed_buf = bson_malloc0 (compressed_len);
   iov.iov_len = compressed_len;
   iov.iov_base = compressed_buf;
   assert (compressed_len == mongoc_gridfs_file_readv (file, &iov, 1, -1, 0));
   mongoc_gridfs_file_destroy (file);

   /* uncompress data using utility function at single shot and ensure it's same as original data */
   uncompressed_len = DATA_LEN;
   uncompressed_buf = bson_malloc0 (uncompressed_len);
   assert (Z_OK == uncompress (uncompressed_buf, &uncompressed_len, compressed_buf, compressed_len));
   assert (DATA_LEN == uncompressed_len);
   assert (memcmp (data_buf, uncompressed_buf, DATA_LEN) == 0);

   /* test read compressed data using buf */
   file = mongoc_gridfs_find_one_by_filename(gridfs, filename, &error);
   assert(file);

   stream = mongoc_stream_cnv_gridfs_new (file, MONGOC_CNV_COMPRESS);
   i = 0;
   while (1) {
      ssize_t nread = mongoc_stream_read (stream, buf, sizeof buf, -1, 0);
      assert (nread >= 0);
      if (nread == 0)
         break;
      assert (memcmp (data_buf + i, buf, nread) == 0);
      i += nread;
   }
   assert (DATA_LEN == i);
   mongoc_stream_destroy (stream);

   /* teardown */
   mongoc_gridfs_file_destroy (file);
   mongoc_gridfs_destroy (gridfs);
   mongoc_client_destroy (client);
   bson_free (data_buf);
   bson_free (compressed_buf);
   bson_free (uncompressed_buf);
}


static void
cleanup_globals (void)
{
   bson_free (gTestUri);
}


void
test_cnv_stream_install (TestSuite *suite)
{
   gTestUri = bson_strdup_printf ("mongodb://%s/", MONGOC_TEST_HOST);

   TestSuite_Add (suite, "/Cnv_Stream/write/compressed", test_write_compressed);
   TestSuite_Add (suite, "/Cnv_Stream/read/compressed", test_read_compressed);
   TestSuite_Add (suite, "/Cnv_Stream/write/10mb", test_write_10mb);

   atexit (cleanup_globals);
}
