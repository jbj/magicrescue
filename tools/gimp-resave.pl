#!/usr/bin/perl
use strict;

use Cwd;
use Fcntl qw(:seek);
use Gimp qw(:auto);
Gimp::init;

my $exitval = 1;

unless (@ARGV) {
    print "Usage: $0 FILENAME

Before using this you must run the Gimp perl server:
\$ gimp --batch '(extension-perl-server 0 0 0)'\n";
    exit 1;
}
my $file = $ARGV[0];

# The Gimp server might be in a different working directory than this script
if ($file !~ m[^/]) {
    $file = getcwd() ."/$file";
}

# Open the file and see if it's completely corrupted
open FH, $file or die "Opening $file: $!\n";
seek FH, 14, SEEK_CUR or die "seek: $!\n";
my $buf;
12 == read FH, $buf, 12 or die "read: $!\n";
close FH;

my ($x, $y, $mode) = unpack("N3", $buf);
if ($x > 10_000 or $y > 10_000 or $mode > 10) {
    print STDERR "$0: bad image: $x x $y, mode $mode\n";
    exit 1;
}

# Prevent message boxes popping up all over the place
gimp_message_set_handler(1);

my $img = gimp_xcf_load(0, $file, $file);

eval {
    my $layer = gimp_image_get_active_layer($img);
    gimp_xcf_save(0, $layer, $file, $file);
    $exitval = 0;
    print STDERR "Successfully resaved image\n";
};
if ($@) {
    print STDERR "$@";
}

# This must always be called, or Gimp will leak memory
gimp_image_delete($img);

Gimp::end;
exit $exitval;
