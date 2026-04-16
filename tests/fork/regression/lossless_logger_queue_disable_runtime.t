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
my $artifact_dir = build_artifact_dir('lossless_logger_queue_disable_runtime');
my $config_yaml = File::Spec->catfile($artifact_dir, 'queue.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(74);
my $stats_port = pick_port(75);

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
    "disable-log-queue = true\n",
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/burst?n=1&size=16'), 'disable queue runtime profile becomes reachable');

my $before = fetch_stats($stats_port);
my $before_enqueues = stat_value($before, 'log_queue_enqueue_events');
my $before_flushes = stat_value($before, 'log_queue_flush_events');
my $before_depth = stat_value($before, 'log_queue_depth');
my $before_req_enqueues = stat_value($before, 'req_log_queue_enqueue_events');
my $before_drops = stat_value($before, 'log_dropped_messages');

my $resp = http_get(
    host => '127.0.0.1',
    port => $port,
    path => '/burst?n=4&size=256',
    timeout => 20,
);

is($resp->{status}, 200, 'disable-log-queue request returns 200');
like($resp->{content}, qr/burst=4 size=256/, 'disable-log-queue PSGI app confirms request');

select undef, undef, undef, 0.35;

my $after = fetch_stats($stats_port);
my $after_enqueues = stat_value($after, 'log_queue_enqueue_events');
my $after_flushes = stat_value($after, 'log_queue_flush_events');
my $after_depth = stat_value($after, 'log_queue_depth');
my $after_req_enqueues = stat_value($after, 'req_log_queue_enqueue_events');
my $after_drops = stat_value($after, 'log_dropped_messages');

is($after_enqueues, $before_enqueues, 'generic queue enqueue counter stays idle when disable-log-queue is set');
is($after_flushes, $before_flushes, 'generic queue flush counter stays idle when disable-log-queue is set');
is($after_depth, $before_depth, 'generic queue depth stays unchanged when disable-log-queue is set');
is($after_req_enqueues, $before_req_enqueues, 'request queue enqueue counter stays idle when disable-log-queue is set');
is($after_drops, $before_drops, 'disable-log-queue does not introduce log drops');

my $log_text = slurp($server_log);
like($log_text, qr/payload burst line 4/, 'logs still reach the configured sink when disable-log-queue is set');

done_testing();
