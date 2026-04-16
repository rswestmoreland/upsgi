use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile slurp start_server stop_server wait_http);

sub send_raw_request {
    my (%args) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => $args{host},
        PeerPort => $args{port},
        Proto    => 'tcp',
        Timeout  => ($args{timeout} || 3),
    ) or die "unable to connect: $!\n";
    print {$sock} $args{payload};
    shutdown($sock, 1);
    my $resp = '';
    while (1) {
        my $chunk = '';
        my $read = sysread($sock, $chunk, 4096);
        last unless $read;
        $resp .= $chunk;
        last if length($resp) > 8192;
    }
    close $sock;
    return $resp;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('fault_request_hardening');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(20);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_header_echo.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'server becomes reachable');

my $dup_host_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "GET / HTTP/1.1\r\nHost: localhost\r\nHost: example.invalid\r\nConnection: close\r\n\r\n",
);
ok($dup_host_resp eq '' || $dup_host_resp !~ m{\AHTTP/1\.[01] 200\b}, 'duplicate Host request is not accepted as 200');

my $dup_cl_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nContent-Length: 4\r\nConnection: close\r\n\r\nabc",
);
ok($dup_cl_resp eq '' || $dup_cl_resp !~ m{\AHTTP/1\.[01] 200\b}, 'duplicate Content-Length request is not accepted as 200');

my $dup_te_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: identity\r\nConnection: close\r\n\r\n0\r\n\r\n",
);
ok($dup_te_resp eq '' || $dup_te_resp !~ m{\AHTTP/1\.[01] 200\b}, 'duplicate Transfer-Encoding request is not accepted as 200');

my $dup_ct_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Type: text/html\r\nContent-Length: 3\r\nConnection: close\r\n\r\nabc",
);
ok($dup_ct_resp eq '' || $dup_ct_resp !~ m{\AHTTP/1\.[01] 200\b}, 'duplicate Content-Type request is not accepted as 200');

my $dup_xtest_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "GET / HTTP/1.1\r\nHost: localhost\r\nX-Test: one\r\nX-Test: two\r\nConnection: close\r\n\r\n",
);
ok($dup_xtest_resp eq '' || $dup_xtest_resp !~ m{\AHTTP/1\.[01] 200\b}, 'duplicate non-list request header is not accepted as 200');

my $folded_header_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "GET / HTTP/1.1\r\nHost: localhost\r\nX-Test: one\r\n two\r\nConnection: close\r\n\r\n",
);
ok($folded_header_resp eq '' || $folded_header_resp !~ m{\AHTTP/1\.[01] 200\b}, 'folded request headers are not accepted as 200');

my $te_cl_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nContent-Length: 4\r\nConnection: close\r\n\r\n0\r\n\r\n",
);
ok($te_cl_resp eq '' || $te_cl_resp !~ m{\AHTTP/1\.[01] 200\b}, 'chunked plus Content-Length request is not accepted as 200');

my $merged_accept_language = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "GET / HTTP/1.1\r\nHost: localhost\r\nAccept-Language: en-US\r\nAccept-Language: fr-CA\r\nConnection: close\r\n\r\n",
);
like($merged_accept_language, qr{\AHTTP/1\.[01] 200\b}, 'duplicate list-style Accept-Language request is accepted');
like($merged_accept_language, qr/http_accept_language=en-US, fr-CA/, 'list-style duplicate request headers are merged once for PSGI env');

my $healthy = http_get(host => '127.0.0.1', port => $port, path => '/');
is($healthy->{status}, 200, 'server still answers a normal request after rejected requests');
like($healthy->{content}, qr/http_accept_language=/, 'normal request still reaches PSGI app');

my $log_text = slurp($server_log);
like($log_text, qr/duplicate HOST header/, 'log records duplicate Host rejection');
like($log_text, qr/duplicate CONTENT_LENGTH header/, 'log records duplicate Content-Length rejection');
like($log_text, qr/duplicate TRANSFER_ENCODING header/, 'log records duplicate Transfer-Encoding rejection');
like($log_text, qr/duplicate CONTENT_TYPE header/, 'log records duplicate Content-Type rejection');
like($log_text, qr/duplicate X_TEST header/, 'log records duplicate non-list header rejection');
like($log_text, qr/folded headers are unsupported/, 'log records folded header rejection');
like($log_text, qr/chunked Transfer-Encoding with Content-Length/, 'log records chunked plus Content-Length rejection');

done_testing();
