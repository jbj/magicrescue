#!/usr/bin/perl -w
use strict;

if (@ARGV == 0 or $ARGV[0] eq "--help") {
    print
q(This script will test magicrescue on existing files on your hard disk to see
if your recipe is good enough. It can be very useful when creating or modifying
a recipe.

Usage:
./provemewrong.pl ./magicrescue -M [options] [files]

Usage examples:

find / -name \*.png -print0 |\
xargs -0 ./provemewrong.pl ./magicrescue -M -m recipes/png -d /tmp/test-output

  or

slocate \*.png|grep -v "['\"]"|\
xargs ./provemewrong.pl ./magicrescue -M -m recipes/png -d /tmp/test-output
);
}

if (0 == open PIPE, "-|") {
    open STDERR, ">/dev/null";
    exec @ARGV or die "Executing $ARGV[0]: $!\n";
}

my ($curfile, $cur_ok);
while (<PIPE>) {
    if (/^i (.*)$/) {
	if ($curfile and !$cur_ok) {
	    print "$curfile: not extracted\n";
	}
	$curfile = $1;
	$cur_ok = 0;
    } elsif (/^o \d+ (.*)$/) {
	if ($curfile and $cur_ok) {
	    print "$curfile: extracted again\n";
	}
	$cur_ok = 1;
	unlink $1 or warn "unlinking $1: $!";
    }
}

