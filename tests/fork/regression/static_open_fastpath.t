use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use JSON::PP qw(decode_json);
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http);

sub fetch_stats {
    my ($port) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $port,
        Proto => 'tcp',
        Timeout => 5,
    ) or die "unable to connect to stats socket on port $port: $!\n";

    my $json = '';
    while (1) {
        my $buf = '';
        my $read = sysread($sock, $buf, 4096);
        last if !defined($read) || $read == 0;
        $json .= $buf;
    }
    close $sock;

    return decode_json($json);
}

sub worker_core {
    my ($stats) = @_;
    return $stats->{workers}[0]{cores}[0];
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('static_open_fastpath');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(62);
my $stats_port = pick_port(63);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_simple.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "stats = 127.0.0.1:$stats_port\n",
    "cache2 = name=staticpaths,items=64,blocksize=4096\n",
    "static-cache-paths = 60\n",
    "static-cache-paths-name = staticpaths\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/assets/hello.txt'), 'static open fastpath profile becomes reachable');

my $before = worker_core(fetch_stats($stats_port));

for (1 .. 3) {
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/hello.txt');
    is($resp->{status}, 200, 'direct static file hit returns 200');
}

my $after = worker_core(fetch_stats($stats_port));

cmp_ok(
    $after->{static_path_cache_hits},
    '>=',
    $before->{static_path_cache_hits} + 3,
    'direct static GET hits reuse the static path cache',
);

cmp_ok(
    $after->{static_open_calls},
    '>=',
    $before->{static_open_calls} + 3,
    'direct static GET hits increment static open calls',
);

is(
    $after->{static_stat_calls},
    $before->{static_stat_calls},
    'direct static GET hits avoid extra static stat calls on the fast path',
);

is(
    $after->{static_index_checks},
    $before->{static_index_checks},
    'direct static GET hits do not trigger index checks on the fast path',
);

done_testing();
