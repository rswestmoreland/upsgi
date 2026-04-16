use strict;
use warnings;
use JSON::PP qw(encode_json);
use vars qw($app);

my %download_sizes = (
    '/download/256k' => 256 * 1024,
    '/download/1m'   => 1024 * 1024,
    '/download/8m'   => 8 * 1024 * 1024,
);
my %download_cache;

sub payload_for {
    my ($size) = @_;
    return $download_cache{$size} if exists $download_cache{$size};

    my $chunk = "upsgi-A4-download-payload\n";
    my $buf = '';
    while (length($buf) < $size) {
        $buf .= $chunk;
    }
    substr($buf, $size) = '' if length($buf) > $size;
    $download_cache{$size} = $buf;
    return $download_cache{$size};
}

$app = sub {
    my ($env) = @_;
    my $path = $env->{PATH_INFO} || '/';
    my $method = $env->{REQUEST_METHOD} || 'GET';

    if ($method eq 'GET' && $path eq '/small') {
        return [200, ['Content-Type' => 'text/plain', 'X-Perf-Scenario' => 'A4'], ["ok\n"]];
    }

    if ($method eq 'GET' && $path eq '/api/ping') {
        return [
            200,
            ['Content-Type' => 'application/json', 'Cache-Control' => 'no-store', 'X-Perf-Scenario' => 'A4'],
            [ encode_json({ ok => 1, scenario => 'A4' }) ],
        ];
    }

    if ($method eq 'GET' && exists $download_sizes{$path}) {
        my $size = $download_sizes{$path};
        my $body = payload_for($size);
        return [
            200,
            [
                'Content-Type' => 'application/octet-stream',
                'Content-Length' => length($body),
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A4',
                'X-Download-Bytes' => $size,
            ],
            [ $body ],
        ];
    }

    return [404, ['Content-Type' => 'text/plain'], ["not found\n"]];
};
