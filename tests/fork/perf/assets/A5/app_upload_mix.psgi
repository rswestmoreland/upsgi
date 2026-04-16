use strict;
use warnings;
use JSON::PP qw(encode_json);
use vars qw($app);

sub read_body {
    my ($input) = @_;
    my $body = '';
    return $body unless $input;

    while (1) {
        my $chunk = '';
        my $read = $input->read($chunk, 65536);
        last unless defined $read && $read > 0;
        $body .= $chunk;
    }
    return $body;
}

sub checksum32 {
    my ($body) = @_;
    my $sum = 0;
    $sum = ($sum + ord($_)) & 0xffffffff for split //, $body;
    return sprintf('%08x', $sum);
}

$app = sub {
    my ($env) = @_;
    my $path = $env->{PATH_INFO} || '/';
    my $method = $env->{REQUEST_METHOD} || 'GET';

    if ($method eq 'GET' && $path eq '/small') {
        return [200, ['Content-Type' => 'text/plain', 'X-Perf-Scenario' => 'A5'], ["ok\n"]];
    }

    if ($method eq 'POST' && $path eq '/upload') {
        my $body = read_body($env->{'psgi.input'});
        return [
            200,
            [
                'Content-Type' => 'application/json',
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A5',
                'X-Body-Length' => length($body),
                'X-Body-Checksum32' => checksum32($body),
            ],
            [ encode_json({ ok => 1, scenario => 'A5', body_length => length($body), checksum32 => checksum32($body) }) ],
        ];
    }

    return [404, ['Content-Type' => 'text/plain'], ["not found\n"]];
};
