#include <mongoc.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include <mongoc-stream-cnv-gridfs.h>

int main (int argc, char *argv[])
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_file_t *file;
   mongoc_gridfs_file_list_t *list;
   mongoc_gridfs_file_opt_t opt = { 0 };
   mongoc_client_t *client;
   mongoc_stream_t *stream;
   bson_t query;
   bson_t child;
   bson_error_t error;
   ssize_t r;
   char buf[4096];
   mongoc_iovec_t iov;
   const char * filename;
   const char * command;

   if (argc < 2) {
      fprintf(stderr, "usage - %s command ...\n", argv[0]);
      return 1;
   }

   mongoc_init();

   iov.iov_base = (void *)buf;
   iov.iov_len = sizeof buf;

   /* connect to localhost client */
   client = mongoc_client_new ("mongodb://127.0.0.1:27017");
   assert(client);

   /* grab a gridfs handle in test prefixed by fs */
   gridfs = mongoc_client_get_gridfs (client, "test", "fs", &error);
   assert(gridfs);

   command = argv[1];
   filename = argv[2];

   if (strcmp(command, "read") == 0) {
      mongoc_stream_t *fstream;

      if (argc != 3) {
         fprintf(stderr, "usage - %s read filename\n", argv[0]);
         return 1;
      }
      file = mongoc_gridfs_find_one_by_filename(gridfs, filename, &error);
      assert(file);

      fstream = mongoc_stream_file_new_for_path (argv [2], O_CREAT | O_WRONLY | O_TRUNC, _S_IWRITE | _S_IREAD);
      assert (fstream);

      stream = mongoc_stream_cnv_gridfs_new (file, MONGOC_CNV_COMPRESS);
      assert(stream);

      for (;;) {
         r = mongoc_stream_read (stream, iov.iov_base, sizeof buf, -1, 0);

         assert (r >= 0);

         if (r == 0) {
            break;
         }
         iov.iov_len = r;
         if (mongoc_stream_writev (fstream, &iov, 1, 0) == -1) {
            MONGOC_ERROR ("Failed to write to file. Exiting.\n");
            exit (1);
         }
      }

      mongoc_stream_destroy (fstream);
      mongoc_stream_destroy (stream);
      mongoc_gridfs_file_destroy (file);
   } else if (strcmp(command, "list") == 0) {
      bson_init (&query);
      bson_append_document_begin (&query, "$orderby", -1, &child);
      bson_append_int32 (&child, "filename", -1, 1);
      bson_append_document_end (&query, &child);
      bson_append_document_begin (&query, "$query", -1, &child);
      bson_append_document_end (&query, &child);

      list = mongoc_gridfs_find (gridfs, &query);

      bson_destroy (&query);

      while ((file = mongoc_gridfs_file_list_next (list))) {
         const char * name = mongoc_gridfs_file_get_filename(file);
         printf("%s\n", name ? name : "?");

         mongoc_gridfs_file_destroy (file);
      }

      mongoc_gridfs_file_list_destroy (list);
   } else if (strcmp(command, "write") == 0) {
      mongoc_stream_t *fstream;

      if (argc != 4) {
         fprintf(stderr, "usage - %s write filename input_file\n", argv[0]);
         return 1;
      }

      fstream = mongoc_stream_file_new_for_path (argv [3], O_RDONLY, 0);
      assert (fstream);

      opt.filename = filename;

      file = mongoc_gridfs_create_file (gridfs, &opt);
      stream = mongoc_stream_cnv_gridfs_new (file, MONGOC_CNV_COMPRESS);

      for (;; ) {
        r = mongoc_stream_read (fstream, iov.iov_base, sizeof buf,
                                0, 0);

        assert (r >= 0);
        if (r == 0) {
            break;
        }
        iov.iov_len = r;
        mongoc_stream_writev (stream, &iov, 1, 0);
      }

      mongoc_stream_destroy (stream);
      mongoc_stream_destroy (fstream);

      mongoc_gridfs_file_destroy(file);
   } else {
      fprintf(stderr, "Unknown command");
      return 1;
   }

   mongoc_gridfs_destroy (gridfs);
   mongoc_client_destroy (client);

   mongoc_cleanup ();

   return 0;
}
