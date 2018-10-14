#!/usr/bin/env perl
use strict;

my $min_size = 99;

my $out = shift or die;

cat($out, $min_size);
if (0 == open READELF, "-|") {
    open STDERR, ">/dev/null";
    exec qw(readelf -h), $out or die;
}

my ($offset, $shsize, $shcount) = (0, 0, 0);
while (<READELF>) {
    if    (/Start of section headers.*?(\d+)/)  { $offset  = $1 }
    elsif (/Size of section headers.*?(\d+)/)   { $shsize  = $1 }
    elsif (/Number of section headers.*?(\d+)/) { $shcount = $1 }
}
close READELF;
wait;

my $size = $offset + $shsize * $shcount;
if ($offset and $shsize and $shcount
	and $size > $min_size and $size < 20_000_000) {

    cat($out, $size - $min_size);
    chmod 0755, $out;

    if (0 == fork) {
	open STDOUT, ">/dev/null";
	open STDERR, ">&STDOUT";
	exec qw(objdump -x), $out or die;
    }
    wait;
    
    if ($?) {
	print "objdump exit status $?, removing output\n";
	unlink $out;
    }
}

sub cat {
    my ($file, $bytes) = @_;
    my $blocksize = 10240;
    my $buf;

    open OUT, ">>$file" or die;
    for (my $written = 0; $written < $bytes; $written += $blocksize) {
	my $left = $bytes - $written;
	if ($left > $blocksize) { $left = $blocksize }
	
	read STDIN, $buf, $left or die;
	print OUT $buf or die;
    }
    close OUT;
}
