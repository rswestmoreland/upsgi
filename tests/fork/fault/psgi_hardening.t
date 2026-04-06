use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile slurp start_server stop_server wait_http);

sub raw_http_get {
    my (%args) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => $args{host},
        PeerPort => $args{port},
        Proto => 'tcp',
        Timeout => ($args{timeout} || 5),
    ) or die "unable to connect to test server\n";

    my $path = $args{path} || '/';
    my $request = "GET $path HTTP/1.1\r\nHost: $args{host}:$args{port}\r\nConnection: close\r\n\r\n";
    print {$sock} $request or die "unable to write test request\n";

    my $raw = '';
    while (my $line = <$sock>) {
        $raw .= $line;
    }
    close $sock;
    return $raw;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('fault_psgi_hardening');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(21);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => fixture_app('app_bad_response.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

start_server(
    binary => $binary,
    config_ini => $config_ini,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/ok'), 'server becomes reachable');

my $ok = http_get(host => '127.0.0.1', port => $port, path => '/ok');
is($ok->{status}, 200, 'control request returns 200');
like($ok->{content}, qr/^ok$/m, 'control request reaches fixture app');

my $dup = raw_http_get(host => '127.0.0.1', port => $port, path => '/dup-set-cookie');
is(scalar(() = $dup =~ /^Set-Cookie:/mg), 2, 'duplicate response headers are emitted as separate lines');
like($dup, qr/^HTTP\/1\.[01] 200 /m, 'duplicate-header response remains a normal success');

for my $path (qw(
    /bad-status
    /bad-arity
    /bad-headers
    /bad-header-name
    /bad-header-value
    /missing-content-type
    /forbidden-content-type
    /forbidden-content-length
    /bad-body-hole
    /bad-body-wide
    /bad-path
    /bad-logger
    /bad-stream
)) {
    my $resp = http_get(host => '127.0.0.1', port => $port, path => $path);
    ok($resp->{status} >= 500 || !$resp->{success}, "$path does not return a normal success response");
}

my $after = http_get(host => '127.0.0.1', port => $port, path => '/ok');
is($after->{status}, 200, 'server still answers a normal request after malformed PSGI responses');

my $log_text = slurp($server_log);
like($log_text, qr/invalid PSGI status code/, 'log records invalid PSGI status code');
like($log_text, qr/invalid PSGI response arity/, 'log records invalid PSGI response arity');
like($log_text, qr/invalid PSGI headers/, 'log records invalid PSGI headers');
like($log_text, qr/missing PSGI Content-Type/, 'log records missing PSGI Content-Type');
like($log_text, qr/forbidden PSGI Content-Type for status/, 'log records forbidden PSGI Content-Type');
like($log_text, qr/forbidden PSGI Content-Length for status/, 'log records forbidden PSGI Content-Length');
like($log_text, qr/invalid PSGI response body/, 'log records invalid PSGI response body');
like($log_text, qr/psgix\.logger requires a hash reference argument/, 'log records invalid psgix.logger usage');
like($log_text, qr/streaming responder requires an array reference/, 'log records invalid streaming responder usage');

done_testing();
