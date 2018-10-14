#!/usr/bin/env perl
use strict;

my $max_file_len = 10_000_000;

my ($buf, $len, $type);

read(STDIN, $buf, 8) == 8 or die "header read error: $!\n";
$buf eq "\211PNG\r\n\032\n" or die "bad magic\n";

print $buf;
my $written = 8;

while (read(STDIN, $buf, 4) == 4) {
    $len = unpack("N", $buf) + 8;
    if ($len > $max_file_len) {
	die "Invalid chunk length $len\n";
    }

    print $buf;
    if (read(STDIN, $buf, $len) != $len) {
	die "read error: $!\n";
    }
    $written += $len + 4;
    if ($written > $max_file_len) {
	print STDERR "File too long, aborting\n";
	exit 1;
    }

    $type = unpack("a4", $buf);
    if ($type !~ /^[a-zA-Z]{4}$/) {
	print STDERR "Invalid type code, aborting\n";
	exit 1;
    }
    print $buf;

    if ($type eq 'IEND') {
	print STDERR "Successfully extracted png file\n";
	exit 0;
    }
}

