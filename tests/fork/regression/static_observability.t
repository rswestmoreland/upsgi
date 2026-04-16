use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fetch_stats_json fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http);

sub worker_core {
    my ($stats) = @_;
    return $stats->{workers}[0]{cores}[0];
}

sub worker_totals {
    my ($stats) = @_;
    return $stats->{workers}[0];
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('static_observability');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(42);
my $stats_port = pick_port(43);

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
    "static-index = index.js\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/assets/hello.txt'), 'static observability profile becomes reachable');

my $before_stats = fetch_stats_json(port => $stats_port);
my $before_core = worker_core($before_stats);
my $before_worker = worker_totals($before_stats);

for (1 .. 3) {
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/hello.txt');
    is($resp->{status}, 200, 'static file hit returns 200');
}

for (1 .. 2) {
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/nested/');
    is($resp->{status}, 200, 'directory index hit returns 200');
    like($resp->{content}, qr/upsgi static nested ok/, 'directory index body is returned');
}

my $fallback_resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/missing.txt');
is($fallback_resp->{status}, 200, 'missing static file falls back to PSGI app');
like($fallback_resp->{content}, qr/upsgi simple ok/, 'missing static file reaches PSGI fallback app');

my $after_stats = fetch_stats_json(port => $stats_port);
my $after_core = worker_core($after_stats);
my $after_worker = worker_totals($after_stats);

cmp_ok(
    $after_core->{static_path_cache_hits},
    '>=',
    $before_core->{static_path_cache_hits} + 3,
    'per-core static path cache hit counter increases for repeated static requests',
);
cmp_ok(
    $after_core->{static_path_cache_misses},
    '>=',
    $before_core->{static_path_cache_misses} + 2,
    'per-core static path cache miss counter increases for first nested-hit and missing-path resolution',
);
cmp_ok(
    $after_core->{static_realpath_calls},
    '>=',
    $before_core->{static_realpath_calls} + 2,
    'per-core static realpath counter increases when uncached nested and missing paths require resolution',
);
cmp_ok(
    $after_core->{static_stat_calls},
    '>=',
    $before_core->{static_stat_calls} + 2,
    'per-core static stat counter increases for the uncached directory stat plus the directory-index probe',
);
cmp_ok(
    $after_core->{static_open_calls},
    '>=',
    $before_core->{static_open_calls} + 5,
    'per-core static open counter increases for direct-file static responses',
);
cmp_ok(
    $after_core->{static_open_failures},
    '>=',
    $before_core->{static_open_failures},
    'per-core static open failure counter does not regress under normal static traffic',
);
cmp_ok(
    $after_core->{static_index_checks},
    '>=',
    $before_core->{static_index_checks} + 1,
    'per-core static index counter increases for the uncached directory index probe',
);

is($after_worker->{static_path_cache_hits}, $after_core->{static_path_cache_hits}, 'worker aggregate cache hits match the lone core');
is($after_worker->{static_path_cache_misses}, $after_core->{static_path_cache_misses}, 'worker aggregate cache misses match the lone core');
is($after_worker->{static_realpath_calls}, $after_core->{static_realpath_calls}, 'worker aggregate realpath calls match the lone core');
is($after_worker->{static_stat_calls}, $after_core->{static_stat_calls}, 'worker aggregate stat calls match the lone core');
is($after_worker->{static_open_calls}, $after_core->{static_open_calls}, 'worker aggregate open calls match the lone core');
is($after_worker->{static_open_failures}, $after_core->{static_open_failures}, 'worker aggregate open failures match the lone core');
is($after_worker->{static_index_checks}, $after_core->{static_index_checks}, 'worker aggregate index checks match the lone core');

done_testing();
