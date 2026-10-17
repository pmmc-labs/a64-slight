# deriv, after Gabriel's: an expression is an array, a symbol a string.
use strict;
use warnings;

sub deriv {
    my $e = shift;
    return $e eq 'x' ? 1 : 0 unless ref $e;
    my ($op, @args) = @$e;
    return [$op, map { deriv($_) } @args] if $op eq '+' || $op eq '-';
    return ['*', $e, ['+', map { ['/', deriv($_), $_] } @args]] if $op eq '*';
    return ['-', ['/', deriv($args[0]), $args[1]], ['/', $args[0], ['*', $args[1], $args[1], deriv($args[1])]]] if $op eq '/';
    die "no derivative of $op\n";
}

sub atoms {
    my $e = shift;
    return 1 unless ref $e;
    my $n = 0;
    $n += atoms($_) for @$e;
    return $n;
}

my $e = ['+', ['*', 3, 'x', 'x'], ['*', 'a', 'x', 'x'], ['*', 'b', 'x'], 5];
my ($total, $last) = (0, []);
for (1 .. $ARGV[0]) {
    $last = deriv($e);
    $total += @$last;
}
print "$total\n", atoms($last), "\n";
