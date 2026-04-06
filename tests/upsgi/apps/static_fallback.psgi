use strict;
use warnings;

my $app = sub {
    my $env = shift;
    my $path = defined $env->{PATH_INFO} ? $env->{PATH_INFO} : '';
    my $method = defined $env->{REQUEST_METHOD} ? $env->{REQUEST_METHOD} : '';
    my $body = "psgi-fallback method=$method path=$path\n";
    return [
        200,
        [
            'Content-Type' => 'text/plain',
            'Content-Length' => length($body),
            'X-upsgi-Fallback' => '1',
        ],
        [$body],
    ];
};

return $app;
