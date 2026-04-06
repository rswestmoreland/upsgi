use strict;
use warnings;

return sub {
    my $env = shift;
    my $n = 8;
    if (($env->{QUERY_STRING} || '') =~ /(?:^|&)n=(\d+)/) {
        $n = $1;
    }
    for my $i (1 .. $n) {
        syswrite STDERR, "burst line $i\n";
    }
    return [
        200,
        [ 'Content-Type' => 'text/plain' ],
        [ "burst=$n\n" ],
    ];
};
