use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use Test::More;

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

sub read_until_close {
    my ($sock) = @_;
    my $raw = '';
    while (1) {
        my $chunk = '';
        my $rv = sysread($sock, $chunk, 4096);
        last if !defined($rv) || $rv == 0;
        $raw .= $chunk;
    }
    return $raw;
}

sub raw_request {
    my ($port, $request) = @_;
    my $sock = open_socket($port);
    print {$sock} $request or die "unable to write request\n";
    my $raw = read_until_close($sock);
    close $sock;
    return $raw;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('psgi_informational');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(15);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => fixture_app('app_psgi_informational.psgi'),
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/has-info'), 'server becomes reachable');

my $http11_has = raw_request(
    $port,
    "GET /has-info HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n",
);
like($http11_has, qr/\r\n\r\nhas_info=1\n\z/s, 'HTTP/1.1 request exposes psgix.informational');

my $http10_has = raw_request(
    $port,
    "GET /has-info HTTP/1.0\r\n\r\n",
);
like($http10_has, qr/\r\n\r\nhas_info=0\n\z/s, 'HTTP/1.0 request does not expose psgix.informational');

my $early = raw_request(
    $port,
    "GET /early HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n",
);
like($early, qr/\AHTTP\/1\.1 103 Early Hints\r\n/s, 'raw response starts with informational 103');
like($early, qr/\r\nLink: <\/style\.css>; rel=preload; as=style\r\n/i, '103 response carries Link preload header');
like($early, qr/\r\nX-Early: yes\r\n/i, '103 response carries custom informational header');
like($early, qr/\r\n\r\nHTTP\/1\.1 200 OK\r\n/s, 'final response follows the informational response on the same connection');
like($early, qr/\r\nContent-Type: text\/plain\r\n/i, 'final response still carries normal PSGI headers');
like($early, qr/\r\n\r\nfinal body\n\z/s, 'final response body is delivered after 103');

done_testing();
