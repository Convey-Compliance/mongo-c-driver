==============
mongo-c-driver
==============

Convey mongoc_grigfs_cnv_file_t wrapper
=====

The new functionality provided by this fork allows to compress/uncompress and/or encrypt/decrypt file on the fly while reading/writing from the mongo. 
Original src mongoc-grigfs-file.c modified to provide hooks: right before chunk write in mongo collection and right after read.
mongoc_grigfs_cnv_file_t wrapper uses mongoc_grigfs_file_t internally(object composition) so interface is the same.

Additional functions provided:

* mongoc_gridfs_cnv_file_is_compressed()
* mongoc_gridfs_cnv_file_is_encrypted()
* mongoc_gridfs_cnv_file_get_compressed_length()
* mongoc_gridfs_cnv_file_set_aes_key()
* mongoc_gridfs_cnv_file_set_aes_key_from_password()

Instance of mongoc_grigfs_cnv_file_t can be created using functions (similar to mongoc_grigfs_file_t creation functions):

* mongoc_gridfs_create_cnv_file_from_stream()
* mongoc_gridfs_create_cnv_file()
* mongoc_gridfs_find_one_cnv()
* mongoc_gridfs_find_one_cnv_by_filename()
* mongoc_gridfs_cnv_file_from_file() // additional function to create from mongoc_grigfs_file_t

this functions accepts additional flags parameter to specify compress/uncompress/encrypt/decrypt behavior

See example-cnv-gridfs example to see how to read/write files using cnv gridfs file(example similar to original example-gridfs).
Tests for new functionality available in test-mongoc-gridfs-cnv-file.c

zlib is used for compress/decompress, Brian Gladman's AES implementation(with EAX mode) used for encrypt/decpypt

Data encrypted/compressed chunk-by-chunk, so actual chunk size can be greater or less than file.chunk_size, however because mongoc logic uses file.chunk_size(not actual written chunk size) to calculate offset, seek() function works(even if file size changed due to compression) over compressed file like it's not compressed.
Notice that length of compressed file is saved as original(uncompressed) file length, compressed length saved in metadata.
Because of tricks with file size and chunks size and implementation of mongoc_grigfs_cnv_file_t that depends on those sizes, it's not possible to read compressed file using mongoc_grigfs_cnv_file_t, you should use mongoc_grigfs_cnv_file_t with MONGOC_CNV_NONE flag.


About
=====

mongo-c-driver is a client library written in C for MongoDB.

mongo-c-driver depends on `Libbson <https://github.com/mongodb/libbson>`_.
Libbson will automatically be built if you do not have it installed on your system.

Documentation / Support / Feedback
==================================

The documentation is available at http://api.mongodb.org/c/current/.
For issues with, questions about, or feedback for libmongoc, please look into
our `support channels <http://www.mongodb.org/about/support>`_. Please
do not email any of the libmongoc developers directly with issues or
questions - you're more likely to get an answer on the `mongodb-user list`_
on Google Groups.

Bugs / Feature Requests
=======================

Think you’ve found a bug? Want to see a new feature in libmongoc? Please open a
case in our issue management tool, JIRA:

- `Create an account and login <https://jira.mongodb.org>`_.
- Navigate to `the CDRIVER project <https://jira.mongodb.org/browse/CDRIVER>`_.
- Click **Create Issue** - Please provide as much information as possible about the issue type and how to reproduce it.

Bug reports in JIRA for all driver projects (i.e. CDRIVER, CSHARP, JAVA) and the
Core Server (i.e. SERVER) project are **public**.

How To Ask For Help
-------------------

If you are having difficulty building the driver after reading the below instructions, please email
the `mongodb-user list`_ to ask for help. Please include in your email all of the following
information:

- The version of the driver you are trying to build (branch or tag).
    - Examples: master branch, 1.2.1 tag
- Host OS, version, and architecture.
    - Examples: Windows 8 64-bit x86, Ubuntu 12.04 32-bit x86, OS X Mavericks
- C Compiler and version.
    - Examples: GCC 4.8.2, MSVC 2013 Express, clang 3.4, XCode 5
- The output of ``./autogen.sh`` or ``./configure`` (depending on whether you are building from a
  repository checkout or from a tarball). The output starting from "libbson was configured with
  the following options" is sufficient.
- The text of the error you encountered.

Failure to include the relevant information will result in additional round-trip
communications to ascertain the necessary details, delaying a useful response.
Here is a made-up example of a help request that provides the relevant
information:

  Hello, I'm trying to build the C driver with SSL, from mongo-c-driver-1.2.1.tar.gz. I'm on Ubuntu
  14.04, 64-bit Intel, with gcc 4.8.2. I run configure like::

    $ ./configure --enable-sasl=yes
    checking for gcc... gcc
    checking whether the C compiler works... yes

    ... SNIPPED OUTPUT, but when you ask for help, include full output without any omissions ...

    checking for pkg-config... no
    checking for SASL... no
    checking for sasl_client_init in -lsasl2... no
    checking for sasl_client_init in -lsasl... no
    configure: error: You must install the Cyrus SASL libraries and development headers to enable SASL support.

  Can you tell me what I need to install? Thanks!

.. _mongodb-user list: http://groups.google.com/group/mongodb-user

Security Vulnerabilities
------------------------

If you’ve identified a security vulnerability in a driver or any other
MongoDB project, please report it according to the `instructions here
<http://docs.mongodb.org/manual/tutorial/create-a-vulnerability-report>`_.


Building the Driver from Source
===============================

Detailed installation instructions are in the manual:
http://api.mongodb.org/c/current/installing.html

From a tarball
--------------

Download the latest release from `the release page <https://github.com/mongodb/mongo-c-driver/releases>`_, then::

  $ tar xzf mongo-c-driver-$ver.tar.gz
  $ cd mongo-c-driver-$ver
  $ ./configure
  $ make
  $ sudo make install

To see all of the options available to you during configuration, run::

  $ ./configure --help

To build on Windows Vista or newer with Visual Studio 2010, do the following::

  cd mongo-c-driver-$ver
  cd src\libbson
  cmake -DCMAKE_INSTALL_PREFIX=C:\usr -G "Visual Studio 10 Win64" .
  msbuild.exe ALL_BUILD.vcxproj
  msbuild.exe INSTALL.vcxproj
  cd ..\..
  cmake -DCMAKE_INSTALL_PREFIX=C:\usr -DBSON_ROOT_DIR=C:\usr -G "Visual Studio 10 Win64" .
  msbuild.exe ALL_BUILD.vcxproj
  msbuild.exe INSTALL.vcxproj

Building From Git
=================

You can use the following to checkout and build mongo-c-driver::

  $ git clone https://github.com/mongodb/mongo-c-driver.git
  $ cd mongo-c-driver
  $ git checkout x.y.z  # To build a particular release
  $ ./autogen.sh --with-libbson=bundled
  $ make
  $ sudo make install
