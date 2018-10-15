#!/usr/bin/perl
use strict;

my $file = shift or die "Usage: script_rename.pl FILE\n";

open SCRIPT, "<", $file or die "Opening $file: $!\n";
my $line1 = <SCRIPT> or die "reading $file: $!\n";
close SCRIPT;

if ($line1 =~ m{^#![^ ]*?([^/ ]+)(?: |$)}) {
    print "RENAME $1\n";
}
