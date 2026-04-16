use strict;
use warnings;

use File::Copy qw(copy);
use File::Path qw(make_path);
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
my $artifact_dir = build_artifact_dir('static_skip_ext_pre_resolve');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $custom_static_root = File::Spec->catdir($artifact_dir, 'static_root');
my $mapped_file = File::Spec->catfile($custom_static_root, 'hello.txt');
my $port = pick_port(50);
my $stats_port = pick_port(51);

make_path($custom_static_root);
copy(File::Spec->catfile(fixture_static_root(), 'hello.txt'), $mapped_file) or die "unable to copy hello.txt fixture: $!\n";

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_simple.psgi'),
    static_root => $custom_static_root,
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "stats = 127.0.0.1:$stats_port\n",
    "cache2 = name=staticpaths,items=64,blocksize=4096\n",
    "static-cache-paths = 60\n",
    "static-cache-paths-name = staticpaths\n",
    "static-skip-ext = .txt\n",
    "static-map = /mapped-noext=$mapped_file\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/not-found'), 'static skip-ext pre-resolve profile becomes reachable');

my $before_stats = fetch_stats($stats_port);
my $before_core = worker_core($before_stats);

for (1 .. 2) {
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/mapped-noext');
    is($resp->{status}, 200, 'pre-resolve skip-ext mapped request falls back to PSGI app');
    is($resp->{headers}{'x-upsgi-app'}, 'simple', 'fallback response identifies the PSGI app');
    like($resp->{content}, qr/upsgi simple ok/, 'fallback response body comes from PSGI app');
}

my $after_stats = fetch_stats($stats_port);
my $after_core = worker_core($after_stats);

is(
    $after_core->{static_path_cache_misses},
    $before_core->{static_path_cache_misses},
    'pre-resolve skip-ext mapped requests avoid static path cache misses',
);
is(
    $after_core->{static_path_cache_hits},
    $before_core->{static_path_cache_hits},
    'pre-resolve skip-ext mapped requests avoid static path cache hits',
);
is(
    $after_core->{static_realpath_calls},
    $before_core->{static_realpath_calls},
    'pre-resolve skip-ext mapped requests avoid realpath calls entirely',
);
is(
    $after_core->{static_stat_calls},
    $before_core->{static_stat_calls},
    'pre-resolve skip-ext mapped requests avoid static stat calls entirely',
);
is(
    $after_core->{static_index_checks},
    $before_core->{static_index_checks},
    'pre-resolve skip-ext mapped requests avoid static index checks entirely',
);

done_testing();
