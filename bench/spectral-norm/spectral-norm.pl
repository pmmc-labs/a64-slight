# spectral-norm, the Benchmarks Game's, single-threaded.
use strict;
use warnings;
use POSIX qw(floor);

sub a {
    my ($i, $j) = @_;
    return 1.0 / (($i + $j) * ($i + $j + 1) / 2 + $i + 1);
}

sub times_a {
    my ($u) = @_;
    my @v;
    for my $i (0 .. $#$u) {
        my $s = 0;
        $s += a($i, $_) * $u->[$_] for 0 .. $#$u;
        push @v, $s;
    }
    return \@v;
}

sub times_at {
    my ($u) = @_;
    my @v;
    for my $i (0 .. $#$u) {
        my $s = 0;
        $s += a($_, $i) * $u->[$_] for 0 .. $#$u;
        push @v, $s;
    }
    return \@v;
}

my $n = $ARGV[0];
my ($u, $v) = ([(1) x $n]);
for (1 .. 10) {
    $v = times_at(times_a($u));
    $u = times_at(times_a($v));
}
my ($vbv, $vv) = (0, 0);
for my $i (0 .. $n - 1) {
    $vbv += $u->[$i] * $v->[$i];
    $vv  += $v->[$i] * $v->[$i];
}
print floor(sqrt($vbv / $vv) * 1e9 + 0.5), "\n";
