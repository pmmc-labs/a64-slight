use strict;
use warnings;

sub tak {
    my ($x, $y, $z) = @_;
    return $y < $x ? tak(tak($x - 1, $y, $z), tak($y - 1, $z, $x), tak($z - 1, $x, $y)) : $z;
}

my $n = $ARGV[0];
print tak(3 * $n, 2 * $n, $n), "\n";
