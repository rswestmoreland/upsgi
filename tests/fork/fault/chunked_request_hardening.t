use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root append_yaml_options http_get render_profile slurp start_server stop_server wait_http);

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
my $artifact_dir = build_artifact_dir('fault_chunked_request_hardening');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(22);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_upload.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);
append_yaml_options(
    $config_yaml,
    'limit-post = 64',
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

my $bad_chunk_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\nZ\r\nhello\r\n0\r\n\r\n",
);
ok($bad_chunk_resp eq '' || $bad_chunk_resp !~ m{\AHTTP/1\.[01] 200\b}, 'non-hex chunk size request is not accepted as 200');

my $missing_chunk_crlf_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhelloX0\r\n\r\n",
);
ok($missing_chunk_crlf_resp eq '' || $missing_chunk_crlf_resp !~ m{\AHTTP/1\.[01] 200\b}, 'chunk data without a CRLF terminator is not accepted as 200');

my $trailers_resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => "POST / HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n1\r\na\r\n0\r\nX-Test: trailer\r\n\r\n",
);
ok($trailers_resp eq '' || $trailers_resp !~ m{\AHTTP/1\.[01] 200\b}, 'chunked trailers are not accepted as 200');

my $healthy = http_get(host => '127.0.0.1', port => $port, path => '/');
is($healthy->{status}, 200, 'server still answers a normal request after rejected chunked requests');
like($healthy->{content}, qr/^$/s, 'healthy follow-up request still reaches the upload app');

my $log_text = slurp($server_log);
like($log_text, qr/Invalid chunked body:/, 'log records malformed chunk line rejection');
like($log_text, qr/missing chunk data terminator/, 'log records missing chunk data terminator rejection');
like($log_text, qr/trailers are unsupported or the terminating CRLF is missing/, 'log records unsupported chunk trailer rejection');

done_testing();
