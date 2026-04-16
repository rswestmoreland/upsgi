use strict;
use warnings;

return sub {
    my $env = shift;
    my $n = 8;
    my $size = 512;
    if (($env->{QUERY_STRING} || '') =~ /(?:^|&)n=(\d+)/) {
        $n = $1;
    }
    if (($env->{QUERY_STRING} || '') =~ /(?:^|&)size=(\d+)/) {
        $size = $1;
    }
    my $payload = ('x' x $size);
    for my $i (1 .. $n) {
        syswrite STDERR, "payload burst line $i $payload\n";
    }
    return [
        200,
        [ 'Content-Type' => 'text/plain' ],
        [ "burst=$n size=$size\n" ],
    ];
};
