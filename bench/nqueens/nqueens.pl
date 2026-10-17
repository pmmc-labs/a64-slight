# nqueens, after Gabriel's: the lists are arrays, copied where the list
# version makes new lists.
use strict;
use warnings;

sub ok {
    my ($row, $dist, $placed) = @_;
    for my $q (@$placed) {
        return 0 if $q == $row + $dist || $q == $row - $dist;
        $dist++;
    }
    return 1;
}

# x the rows still to try, y those passed over for this column, z those
# placed, the last placed first.
sub try_it {
    my ($x, $y, $z) = @_;
    return @$y ? 0 : 1 unless @$x;
    my ($q, @rest) = @$x;
    return (ok($q, 1, $z) ? try_it([@rest, @$y], [], [$q, @$z]) : 0) + try_it(\@rest, [$q, @$y], $z);
}

print try_it([1 .. $ARGV[0]], [], []), "\n";
