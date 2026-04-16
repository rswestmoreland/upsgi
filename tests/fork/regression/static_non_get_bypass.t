use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use JSON::PP qw(decode_json);
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app fixture_static_root http_request pick_port render_profile start_server stop_server wait_http);

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
my $artifact_dir = build_artifact_dir('static_non_get_bypass');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(46);
my $stats_port = pick_port(47);

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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/assets/hello.txt'), 'static non-GET bypass profile becomes reachable');

my $before_stats = fetch_stats($stats_port);
my $before_core = worker_core($before_stats);

my $post_file = http_request(host => '127.0.0.1', port => $port, method => 'POST', path => '/assets/hello.txt');
is($post_file->{status}, 200, 'POST to static file path falls back to PSGI app');
is($post_file->{headers}{'x-upsgi-app'}, 'simple', 'POST fallback response identifies the PSGI app');
is($post_file->{headers}{'x-request-method'}, 'POST', 'PSGI app receives POST request method');
like($post_file->{content}, qr/upsgi simple ok/, 'POST fallback response body comes from PSGI app');

my $post_dir = http_request(host => '127.0.0.1', port => $port, method => 'POST', path => '/assets/nested/');
is($post_dir->{status}, 200, 'POST to static directory path falls back to PSGI app');
is($post_dir->{headers}{'x-upsgi-app'}, 'simple', 'POST directory fallback response identifies the PSGI app');
is($post_dir->{headers}{'x-request-method'}, 'POST', 'PSGI app receives POST for directory path');
like($post_dir->{content}, qr/upsgi simple ok/, 'POST directory fallback response body comes from PSGI app');

my $after_stats = fetch_stats($stats_port);
my $after_core = worker_core($after_stats);

is($after_core->{static_path_cache_hits}, $before_core->{static_path_cache_hits}, 'POST bypass does not increment static cache hits');
is($after_core->{static_path_cache_misses}, $before_core->{static_path_cache_misses}, 'POST bypass does not increment static cache misses');
is($after_core->{static_realpath_calls}, $before_core->{static_realpath_calls}, 'POST bypass does not call realpath in the static path');
is($after_core->{static_stat_calls}, $before_core->{static_stat_calls}, 'POST bypass does not perform static stat calls');
is($after_core->{static_index_checks}, $before_core->{static_index_checks}, 'POST bypass does not perform static index checks');

done_testing();
