use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;
use IO::Socket::INET;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile slurp start_server stop_server wait_http);

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
    return $json;
}

sub stat_value {
    my ($json, $key) = @_;
    return 0 unless $json =~ /"\Q$key\E"\s*:\s*(\d+)/;
    return $1 + 0;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('lossless_logger_queue_runtime');
my $config_yaml = File::Spec->catfile($artifact_dir, 'queue.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(70);
my $stats_port = pick_port(71);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_log_payload_burst.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "log-master = true\n",
    "log-drain-burst = 1\n",
    "log-queue-records = 4\n",
    "log-queue-bytes = 4096\n",
    "stats = 127.0.0.1:$stats_port\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/burst?n=2&size=32'), 'queue runtime profile becomes reachable');

my $before = fetch_stats($stats_port);
my $before_enqueues = stat_value($before, 'log_queue_enqueue_events');
my $before_flushes = stat_value($before, 'log_queue_flush_events');
my $before_full = stat_value($before, 'log_queue_full_events');
my $before_backpressure = stat_value($before, 'log_queue_backpressure_events');
my $before_drops = stat_value($before, 'log_dropped_messages');

my $resp = http_get(
    host => '127.0.0.1',
    port => $port,
    path => '/burst?n=4&size=5000',
    timeout => 20,
);

is($resp->{status}, 200, 'payload burst request returns 200 when queue bytes cap is exceeded');
like($resp->{content}, qr/burst=4 size=5000/, 'payload burst PSGI app confirms oversized request');

select undef, undef, undef, 0.5;

my $after = fetch_stats($stats_port);
my $after_enqueues = stat_value($after, 'log_queue_enqueue_events');
my $after_flushes = stat_value($after, 'log_queue_flush_events');
my $after_full = stat_value($after, 'log_queue_full_events');
my $after_backpressure = stat_value($after, 'log_queue_backpressure_events');
my $after_drops = stat_value($after, 'log_dropped_messages');
my $after_depth = stat_value($after, 'log_queue_depth');
my $after_bytes_max = stat_value($after, 'log_queue_bytes_max');

cmp_ok($after_enqueues, '>=', $before_enqueues + 4, 'queue enqueues increase for oversized payload logs');
cmp_ok($after_flushes, '>=', $before_flushes + 4, 'queue flushes increase for oversized payload logs');
cmp_ok($after_full, '>=', $before_full + 1, 'queue full counter increases when payload lines exceed the queue byte cap');
cmp_ok($after_backpressure, '>=', $before_backpressure + 1, 'queue backpressure counter increases when payload lines exceed the queue byte cap');
is($after_drops, $before_drops, 'logger queue keeps drop counter unchanged when applying bounded backpressure');
is($after_depth, 0, 'queue drains back to zero depth after oversized payload logs flush');
cmp_ok($after_bytes_max, '>=', 5000, 'queue byte high-water mark records oversized payload lines');

my $log_text = slurp($server_log);
like($log_text, qr/payload burst line 4/, 'oversized payload log lines reach the configured sink');

done_testing();
