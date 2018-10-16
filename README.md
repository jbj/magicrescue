Magic Rescue
============

Deprecation notice
------------------

This software is no longer under active development. I will consider merging
pull requests, but I will not myself address any issues raised in GitHub. I
cannot guarantee that I will make new stable releases.

Security notice
---------------

Magic Rescue should only be run in a sandboxed environment. It was written in
2004, a time where the internet was a friendlier place. Magic Rescue works by
invoking programs written in unsafe languages with input from arbitrary data
found on your disk. This exposes a large attack surface to any attacker that is
able to place arbitrary data inside files on your disk, including your browser
cache.

Overview
--------

Magic Rescue scans a block device for file types it knows how to recover and
calls an external program to extract them.  It looks at "magic bytes" in file
contents, so it can be used both as an undelete utility and for recovering a
corrupted drive or partition.  As long as the file data is there, it will find
it.

It works on any file system, but on very fragmented file systems it can only
recover the first chunk of each file.  Practical experience (this program was
not written for fun) shows, however, that chunks of 30-50MB are not uncommon.

Find the latest version at https://github.com/jbj/magicrescue

Building
--------

There are no build requirements other than a C library and a UNIXish system.
To use the dupemap(1) utility, you must have the NDBM compatibility header,
which either comes with your system or the development libraries of GDBM or
Berkeley DB.

./configure && make && make install

The "make install" step is optional.

Using
-----

man magicrescue
