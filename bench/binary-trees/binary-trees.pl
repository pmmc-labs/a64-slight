# binary-trees, single-threaded: a node is an array ref, a leaf's [undef, undef].
use strict;
use warnings;
no warnings 'recursion';

sub make {
    my $d = shift;
    return $d == 0 ? [undef, undef] : [make($d - 1), make($d - 1)];
}

sub check {
    my $n = shift;
    return defined $n->[0] ? 1 + check($n->[0]) + check($n->[1]) : 1;
}

my $top = $ARGV[0] > 6 ? $ARGV[0] : 6;
print check(make($top + 1)), "\n";
my $long = make($top);
for (my $d = 4; $d <= $top; $d += 2) {
    my $iterations = 2 ** ($top - $d + 4);
    my $sum = 0;
    $sum += check(make($d)) for 1 .. $iterations;
    print "$sum\n";
}
print check($long), "\n";
