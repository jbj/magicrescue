#!/usr/bin/env perl
use strict;

# Attempts to guess the file type of an OLE container file and rename it
# accordingly.
# Depends on an ancient perl4 script from 1998 named laola.pl to parse the
# file. It would be better to use the OLE::Storage module from CPAN, but that
# module is broken at the time of writing.

# This is not a hash, because it is important that the extensions are tried in
# order of importance.
my @extensions = (
    WordDocument       => "doc",
    PowerPointDocument => "ppt",
    Workbook           => "xls", # New Excel versions
    StarWriterDocument => "sdw",
    StarCalcDocument   => "sdc",
    StarDrawDocument3  => "sdd", # Could also be .sda
    Book               => "xls", # Excel 5.0
    Quill              => "pub", # Microsoft Publisher
    PP40               => "pot", # PowerPoint Template, or...?
    WPG20              => "wordperfect_unknown", # What is this?
    PerfectOffice_MAIN => "wb3", # Are all such files from Quattro Pro? Some
                                 # may be .shw presentations.
    EquationNative     => "equation",
    StarBaseDocument   => "starbase",
    WorkspaceState     => "opt", # MS Visual Studio
    SentenceExceptList => "staroffice_dictionary",
    StarBASIC          => "starbasic",
    SIG1               => "staroffice_unknown", # What is this?
);
my %extensions = @extensions; # for quick lookup

# When invoked by magicrescue, laola.pl should be in the PATH
push @INC, grep /tools/, split /:/, $ENV{PATH};
require 'laola.pl';

my $file = $ARGV[0];
unless (@ARGV and -f $file) {
    die "Usage: ole_rename.pl FILENAME\n";
}

# LAOLA does not do much sanity checking, a corrupted file can send it into
# a memory-exhausting loop. Using alarm here is basically a hack for systems
# where magicrescue can't do setrlimit to set max memory usage.
$SIG{ALRM} = sub { die "Timed out" };
alarm 10;

my $extension = "";
my $status = laola_open_document($file);
$status eq "ok" or die "laola_open_document failed: $status\n";

foreach my $pps (laola_get_dirhandles(0)) {
    my $name = laola_pps_get_name($pps);
    $name =~ s/[^-\w]//g;

    next unless $extensions{$name};
    for (my $i = 0; $i < @extensions; $i += 2) {
	if ($name eq $extensions[$i]) {
	    $extension = $extensions[$i+1];
	    last;
	    # we keep looking even though we have found the format, because
	    # the real format always seems to be closest to the end.
	}
    }
}
laola_close_document();

alarm 0;

if ($extension) {
    print "RENAME $extension";
}

