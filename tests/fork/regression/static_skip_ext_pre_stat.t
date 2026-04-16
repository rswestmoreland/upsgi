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
my $artifact_dir = build_artifact_dir('static_skip_ext_pre_stat');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $custom_static_root = File::Spec->catdir($artifact_dir, 'static_root');
my $source_file = File::Spec->catfile(fixture_static_root(), 'hello.txt');
my $masked_link = File::Spec->catfile($custom_static_root, 'masked');
my $port = pick_port(48);
my $stats_port = pick_port(49);

make_path($custom_static_root);
copy($source_file, File::Spec->catfile($custom_static_root, 'hello.txt')) or die "unable to copy hello.txt fixture: $!\n";
symlink('hello.txt', $masked_link) or die "unable to create masked symlink: $!\n";

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
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/not-found'), 'static skip-ext pre-stat profile becomes reachable');

my $before_stats = fetch_stats($stats_port);
my $before_core = worker_core($before_stats);

for (1 .. 2) {
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/masked');
    is($resp->{status}, 200, 'skip-ext masked request falls back to PSGI app');
    is($resp->{headers}{'x-upsgi-app'}, 'simple', 'fallback response identifies the PSGI app');
    like($resp->{content}, qr/upsgi simple ok/, 'fallback response body comes from PSGI app');
}

my $after_stats = fetch_stats($stats_port);
my $after_core = worker_core($after_stats);

cmp_ok(
    $after_core->{static_path_cache_misses},
    '>=',
    $before_core->{static_path_cache_misses} + 1,
    'first masked skip-ext request misses the static path cache',
);
cmp_ok(
    $after_core->{static_path_cache_hits},
    '>=',
    $before_core->{static_path_cache_hits} + 1,
    'second masked skip-ext request reuses the static path cache',
);
cmp_ok(
    $after_core->{static_realpath_calls},
    '>=',
    $before_core->{static_realpath_calls} + 1,
    'first masked skip-ext request resolves the target path once',
);
is(
    $after_core->{static_stat_calls},
    $before_core->{static_stat_calls},
    'skip-ext masked requests avoid static stat calls entirely',
);
is(
    $after_core->{static_index_checks},
    $before_core->{static_index_checks},
    'skip-ext masked requests avoid static index checks',
);

done_testing();
