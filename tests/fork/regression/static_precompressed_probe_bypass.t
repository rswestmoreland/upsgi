use strict;
use warnings;

use File::Path qw(make_path);
use File::Spec;
use FindBin;
use IO::Socket::INET;
use JSON::PP qw(decode_json);
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app http_get pick_port render_profile start_server stop_server wait_http);

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
my $artifact_dir = build_artifact_dir('static_precompressed_probe_bypass');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $static_root = File::Spec->catdir($artifact_dir, 'static');
my $port = pick_port(54);
my $stats_port = pick_port(55);

make_path($static_root);
open my $fh, '>', File::Spec->catfile($static_root, 'hello.txt.gz') or die "unable to create precompressed fixture: $!\n";
print {$fh} "already encoded\n";
close $fh;

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_simple.psgi'),
    static_root => $static_root,
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "stats = 127.0.0.1:$stats_port\n",
    "cache2 = name=staticpaths,items=64,blocksize=4096\n",
    "static-cache-paths = 60\n",
    "static-cache-paths-name = staticpaths\n",
    "static-gzip-all = true\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'precompressed static profile becomes reachable');

my $before = worker_core(fetch_stats($stats_port));

my $resp = http_get(
    host => '127.0.0.1',
    port => $port,
    path => '/assets/hello.txt.gz',
    headers => { 'Accept-Encoding' => 'br, gzip' },
);

is($resp->{status}, 200, 'direct request for precompressed asset returns 200');
is($resp->{content}, "already encoded\n", 'direct precompressed asset body is returned unchanged');

my $after = worker_core(fetch_stats($stats_port));

is($after->{static_path_cache_hits}, $before->{static_path_cache_hits}, 'first precompressed request does not report a cache hit');
is($after->{static_path_cache_misses}, $before->{static_path_cache_misses} + 1, 'first precompressed request records one cache miss');
is($after->{static_realpath_calls}, $before->{static_realpath_calls} + 1, 'first precompressed request resolves realpath once');
cmp_ok($after->{static_open_calls}, '>=', $before->{static_open_calls} + 1, 'direct precompressed request increments static open calls');
is($after->{static_stat_calls}, $before->{static_stat_calls}, 'direct precompressed request avoids extra static stat calls on the fast path');
is($after->{static_index_checks}, $before->{static_index_checks}, 'direct precompressed request does not perform index checks');

done_testing();
