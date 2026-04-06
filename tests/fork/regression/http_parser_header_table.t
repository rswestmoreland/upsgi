use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root render_profile slurp start_server stop_server wait_http);

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
        last if length($resp) > 65536;
    }
    close $sock;
    return $resp;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('regression_http_parser_header_table');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(20);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => fixture_app('app_header_echo.psgi'),
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'server becomes reachable');

my @extra_headers;
for my $i (1 .. 24) {
    push @extra_headers, sprintf("X-Extra-%02d: value-%02d", $i, $i);
}
my $payload = join("\r\n",
    'GET / HTTP/1.1',
    'Host: localhost',
    'Accept-Language: en-US',
    'aCcEpT-lAnGuAgE: fr-CA',
    @extra_headers,
    'Connection: close',
    '',
    '',
);
my $resp = send_raw_request(
    host => '127.0.0.1',
    port => $port,
    payload => $payload,
);
like($resp, qr{\AHTTP/1\.[01] 200\b}, 'header table parser handles more than the initial scratch span count');
like($resp, qr/http_accept_language=en-US, fr-CA/, 'case-insensitive list-style duplicate headers still merge correctly');

my $log_text = slurp($server_log);
unlike($log_text, qr/duplicate ACCEPT_LANGUAGE header/, 'allowlisted list-style duplicate was not rejected');

done_testing();
