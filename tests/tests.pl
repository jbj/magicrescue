#!/usr/bin/perl
use strict;

# Globals

use File::Slurp;

my $tmp = "tests/temp";
my $app = "./magicrescue";

# Functions

sub ok($;$) {
    die "test \"$_[1]\" failed\n" unless @_[0];
}

sub test_complex($$) {
    my ($recipe, $file) = @_;

    my $sample = read_file($file) or die "$file: $!\n";
    my $oksize = (grep /^allow_overlap/, read_file("recipes/$recipe"))
	? 0 : length $sample;

    # These numbers depend on magicrescue's buffer being just under 100KB
    for my $offset ((0 .. 100, 102200 .. 102600, 204400 .. 205000)) {
	open my $blob, ">", "$tmp/blob" or die "opening $tmp/blob: $!\n";
	seek $blob, $offset, 0 or die "seek: $!\n";
	print {$blob} $sample;
	close $blob;

	my $blockalign = 0;
	if ($oksize) {
	    $blockalign =  256 if ($offset &  255) == 0;
	    $blockalign =  512 if ($offset &  511) == 0;
	    $blockalign = 1024 if ($offset & 1023) == 0;
	}
	
	eval { test_simple($recipe, "$tmp/blob", $oksize, $blockalign) };
	if ($@) {
	    die "test_complex: offset $offset: $@\n";
	}
    }
}

sub test_simple($$$;$) {
    my ($recipe, $file, $oksize, $blockalign) = @_;

    my $count = 0;
    my $mr;

    if (0 == open $mr, "-|") {
	my @blockalign = $blockalign ? ('-b', $blockalign) : ();
	open STDERR, ">/dev/null";
	exec $app, "-d", $tmp, @blockalign, qw(-Mo -r), $recipe, $file;
	die "Executing $app: $!\n";
    }
    while (<$mr>) {
	chomp;
	if ($oksize and $oksize != -s) {
	    print "test_simple: $recipe: size mismatch: $oksize != ",
		-s _, "\n";
	}
	unlink;
	$count++;
	unless (/^$tmp\/[\dA-Fa-f]+-0\./o) {
	    die "test_simple: $recipe: bad output name: $_\n";
	}
    }
    close $mr;
    wait;

    if ($count != 1) {
	die "test_simple: $recipe: extracted $count times, expected 1\n";
    }
}

#
# Main program
#

if (-f "../$app") {
    chdir "..";
}
-x $app && -d "tests/samples" or die "Stand in the source root\n";
$SIG{INT} = $SIG{TERM} = sub { die "Interrupted\n" };

mkdir $tmp or die "mkdir $tmp: $!\n";

eval {
    my @samples = @ARGV ? @ARGV : sort(read_dir("tests/samples"));

    for my $sample (@samples) {
	next if $sample =~ /^\./ or !-f "tests/samples/$sample";
	
	print "Testing recipe $sample...\n";
	test_complex($sample, "tests/samples/$sample");

	#test_simple($sample, "tests/samples/$sample",
	#    (grep /^allow_overlap/, read_file("recipes/$sample"))
	#    ? 0 : -s "tests/samples/$sample");
    }
};
if ($@) {
    print "$@\n";
} else {
    print "All tests ok\n";
}

if (-d $tmp) {
    for (read_dir($tmp)) {
	unlink "$tmp/$_";
    }
    rmdir $tmp;
}

exit ($@ ? 1 : 0);
