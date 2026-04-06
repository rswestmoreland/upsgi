use strict;
use warnings;

my $app = sub {
    my $env = shift;
    my $path = $env->{PATH_INFO} || '/';
    my $logger = $env->{'psgix.logger'};

    if ($path eq '/error') {
        die "logging smoke requested failure\n";
    }

    if ($path eq '/psgix-log' && $logger) {
        $logger->({
            level => 'info',
            message => "psgix logger hit method=$env->{REQUEST_METHOD} path=$path remote=$env->{REMOTE_ADDR}",
        });
    }

    if ($path eq '/whoami') {
        my $remote = defined $env->{REMOTE_ADDR} ? $env->{REMOTE_ADDR} : '';
        my $xff = defined $env->{HTTP_X_FORWARDED_FOR} ? $env->{HTTP_X_FORWARDED_FOR} : '';
        return [
            200,
            ['Content-Type' => 'text/plain'],
            ["remote=$remote\nxff=$xff\n"],
        ];
    }

    return [
        200,
        ['Content-Type' => 'text/plain'],
        ["ok path=$path\n"],
    ];
};

return $app;
