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
my $artifact_dir = build_artifact_dir('psgi_delayed_responder');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(63);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => fixture_app('app_psgi_delayed_responder.psgi'),
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'delayed responder server becomes reachable');

my $sock = open_socket($port);
print {$sock} "GET / HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n";

my $t0 = time();
ok(!wait_readable($sock, 0.10), 'delayed responder does not write immediately');
ok(wait_readable($sock, 1.0), 'delayed responder eventually becomes readable');
my $t1 = time();
my $raw = sysread_once($sock);
while ($raw !~ /\r\n\r\n/s || $raw !~ /delayed-body\n/s) {
    ok(wait_readable($sock, 1.0), 'remaining delayed response data becomes readable');
    $raw .= sysread_once($sock);
}
like($raw, qr{\AHTTP/1\.[01] 200\b}, 'delayed responder returns 200');
like($raw, qr/Content-Type: text\/plain/i, 'delayed responder returns content type');
like($raw, qr/Content-Length: 13\r\n/i, 'delayed responder returns content length');
like($raw, qr/\r\n\r\ndelayed-body\n\z/s, 'delayed responder sends final body');
ok(($t1 - $t0) >= 0.20, 'delayed responder path defers final response emission');
close $sock;

done_testing();
