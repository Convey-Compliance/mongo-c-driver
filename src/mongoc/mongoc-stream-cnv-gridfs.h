#ifndef MONGOC_STREAM_CNV_GRIDFS_H
#define MONGOC_STREAM_CNV_GRIDFS_H

#include "mongoc-gridfs.h"
#include "mongoc-stream.h"


BSON_BEGIN_DECLS


typedef enum
{
   MONGOC_CNV_NONE      = 0,
   MONGOC_CNV_COMPRESS  = 1 << 1,
   MONGOC_CNV_ENCRYPT   = 1 << 2
} mongoc_stream_cnv_flags_t;


mongoc_stream_t *mongoc_stream_cnv_gridfs_new (mongoc_gridfs_file_t *file, int cnv_flags);


BSON_END_DECLS


#endif