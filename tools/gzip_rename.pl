#!/usr/bin/perl
use strict;

use constant FCONT  => 1<<1;
use constant FEXTRA => 1<<2;
use constant FNAME  => 1<<3;

my $file = $ARGV[0];
unless (@ARGV and -f $file) {
    die "Usage: gzip_rename.pl FILENAME < orig-data\n";
}

my $buf;

read STDIN, $buf, 10 or exit;
my $flags = (unpack "C4", $buf)[3];
exit unless defined $flags;
exit unless $flags & FNAME;

if ($flags & FCONT) {
    read STDIN, $buf, 2 or exit;
}
    
if ($flags & FEXTRA) {
    read STDIN, $buf, 2 or exit;
    my $len = unpack("v", $buf); # unsigned little-endian 16-bit
    exit if $len > 10240;
    read STDIN, $buf, $len or exit;
}

read STDIN, $buf, 130 or exit;
my $origname = unpack("Z130", $buf);
if ($origname and length($origname) < 128 and $origname !~ m[[/\x00-\x1F]]) {
    print "RENAME $origname\n";
}
