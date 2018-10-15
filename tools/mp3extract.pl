#!/usr/bin/env perl
use strict;

# This script tries to decode the first part of the mp3 with mpg123, and if
# that fails it will truncate the mp3 at that failure point.
# I could also use mp3_check, perhaps it is more correct. However, users are
# not likely to have it installed.

my $max_bytes     = 40*1024*1024;
my $min_bytes     = 1024;
my $min_pcm       = 11000;

my $ofile = $ARGV[0] or die;

if (-f "$ofile.pcm" and (-s _) < $min_pcm) {
    unlink $ofile;
    exit 1;
}

copy_out_file();
my $size = find_size();
if ($size) {
    eval { truncate($ofile, $size + 128) };
    if ($@) {
	print STDERR "\n\n\ntruncate() failed: $!\nKilling output\n\n\n\n";
	unlink($ofile);
    }
} else {
    print STDERR "Invalid mp3 file\n";
    unlink($ofile);
}

sub copy_out_file {
    my ($read_now, $read_total) = (0, 0);

    open my $fd, ">>", $ofile or die;
    while ($read_total < $max_bytes
	    and $read_now = read STDIN, my $buf, 10240) {
	$read_total += $read_now;
	print {$fd} $buf or die;
    }
    close $fd;
}

sub find_size {
    my $pid = open my $mpg123, "-|" or mpg123();

    while (<$mpg123>) {
	if (/at offset (0x[a-zA-Z0-9]+)\.$/) {
	    kill TERM => $pid; #TODO: mpg123 could already be dead
	    wait;
	    close $mpg123;

	    my $offset = oct $1;
	    return ($offset < $min_bytes or $offset > $max_bytes)
		?  0 : $offset;
	}
    }

    return (-s $ofile < $max_bytes ? (-s _) - 128 : 0);
}

sub mpg123 {
    open STDERR, ">&STDOUT" or die;
    open STDOUT, ">/dev/null" or die;
    exec qw(mpg123 -s -c), $ofile;
    die "Executing mpg123: $!\n";
}
