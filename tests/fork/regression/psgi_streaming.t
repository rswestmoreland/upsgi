use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Select;
use IO::Socket::INET;
use Test::More;
use Time::HiRes qw(time);

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root pick_port render_profile start_server stop_server wait_http);

sub open_socket {
    my ($port) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $port,
        Proto => 'tcp',
        Timeout => 5,
    ) or die "unable to connect to 127.0.0.1:$port: $!\n";
    return $sock;
}

sub sysread_once {
    my ($sock) = @_;
    my $buf = '';
    my $rv = sysread($sock, $buf, 4096);
    return '' if !defined($rv) || $rv == 0;
    return $buf;
}

sub wait_readable {
    my ($sock, $timeout) = @_;
    my $sel = IO::Select->new($sock);
    my @ready = $sel->can_read($timeout);
    return scalar @ready;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('psgi_streaming');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(54);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_psgi_streaming.psgi'),
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'streaming server becomes reachable');

my $sock = open_socket($port);
print {$sock} "GET / HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n";

my $t0 = time();
my $raw = '';
my $header_end = -1;
while ($header_end < 0) {
    ok(wait_readable($sock, 1.0), 'socket becomes readable while waiting for streaming headers');
    $raw .= sysread_once($sock);
    $header_end = index($raw, "\r\n\r\n");
}
my $t_headers = time();
my $initial_body = substr($raw, $header_end + 4);
my $headers = substr($raw, 0, $header_end);
like($headers, qr{\AHTTP/1\.[01] 200\b}, 'streaming response returns 200');
like($headers, qr/Content-Type: text\/plain/i, 'streaming response sends Content-Type header');
my $body = $initial_body;
while ($body !~ /world\n/) {
    ok(wait_readable($sock, 1.0), 'later streaming body data becomes readable');
    $body .= sysread_once($sock);
}
my $t_second = time();
like($body, qr/hello\nworld\n/s, 'streaming body contains both delayed chunks in order');
ok(($t_second - $t_headers) >= 0.20, 'streaming body completes after a delayed writer path');
close $sock;

done_testing();
