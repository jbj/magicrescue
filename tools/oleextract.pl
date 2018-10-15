#!/usr/bin/perl
use strict;
#
# This script loosely follows the LAOLA file system described at
# http://user.cs.tu-berlin.de/~schwartz/pmh/guide.html. It only looks at the
# "big block depots", as they seem to be sufficient for finding the length of
# the file.

use Fcntl qw(:seek);

my $buf;

# We use sysread/sysseek here to work around problems on cygwin
sysread STDIN, $buf, 512;

if (substr($buf, 0, 8) ne "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1") {
    die "Not an MS OLE file\n";
}

my $num_of_bbd_blocks = unpack("V", substr($buf, 0x2c));
if ($num_of_bbd_blocks == 0 or $num_of_bbd_blocks > 320) {
    # Max 320 bbd means the file can only be 128*512*320 = 20MB
    die "Corrupted file, too many big block depots\n";
}
my $max_block = 128*$num_of_bbd_blocks;

my @bbds    = unpack("V$num_of_bbd_blocks", substr($buf, 0x4c));
my $block_count = 0;

my $prevblock = -1;
my $block_index = 0;

foreach my $block (@bbds) {
    if ($block > $max_block or $block == $prevblock) {
	die "Corrupted file, bbd block number out of bounds\n";
    }

    sysseek STDIN, 512*($block - $prevblock - 1), SEEK_CUR
	or die "sysseek failed: $!\n";
    512 == sysread STDIN, $buf, 512
	or die "read failed: $!\n";
    my @blockmap = unpack("V128", $buf);

    for (my $i = 0; $i < 128; $i++) {
	if ($blockmap[$i] != 0xFFffFFff) {
	    $block_count = $block_index * 128 + $i + 2;
	}

	if ($blockmap[$i] < 0xFFffFFfd and $blockmap[$i] > $max_block) {
	    die "Corrupted file, block number too high\n";
	}
    }

    
    $prevblock = $block;
    $block_index++;
}

if ($block_count > 0) {
    sysseek STDIN, -512*($bbds[@bbds - 1] + 2), SEEK_CUR # seek to first block
	or die "seek failed: $!\n";
    for (1 .. $block_count) {
	512 == sysread STDIN, $buf, 512
	    or die "read failed: $!\n";
	print $buf
	    or die "write failed: $!\n";
    }
}

