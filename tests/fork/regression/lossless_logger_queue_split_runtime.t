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
my $artifact_dir = build_artifact_dir('lossless_logger_queue_split_runtime');
my $config_yaml = File::Spec->catfile($artifact_dir, 'queue.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(72);
my $stats_port = pick_port(73);
my $ua = ('A' x 2000);

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
    "log-queue-bytes = 1024\n",
    "log-format = [req] %(uagent)\n",
    "req-logger = stdio\n",
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/burst?n=1&size=16'), 'split queue runtime profile becomes reachable');

my $before = fetch_stats($stats_port);
my $before_generic_enqueues = stat_value($before, 'log_queue_enqueue_events');
my $before_generic_full = stat_value($before, 'log_queue_full_events');
my $before_generic_depth = stat_value($before, 'log_queue_depth');
my $before_req_enqueues = stat_value($before, 'req_log_queue_enqueue_events');
my $before_req_depth = stat_value($before, 'req_log_queue_depth');
my $before_drops = stat_value($before, 'log_dropped_messages');
my $before_req_drops = stat_value($before, 'req_log_dropped_messages');

my $resp = http_get(
    host => '127.0.0.1',
    port => $port,
    path => '/burst?n=4&size=32',
    timeout => 20,
    headers => {
        'User-Agent' => $ua,
    },
);

is($resp->{status}, 200, 'split queue request returns 200');
like($resp->{content}, qr/burst=4 size=32/, 'split queue PSGI app confirms request');

select undef, undef, undef, 0.5;

my $after = fetch_stats($stats_port);
my $after_generic_enqueues = stat_value($after, 'log_queue_enqueue_events');
my $after_generic_full = stat_value($after, 'log_queue_full_events');
my $after_generic_depth = stat_value($after, 'log_queue_depth');
my $after_req_enqueues = stat_value($after, 'req_log_queue_enqueue_events');
my $after_req_depth = stat_value($after, 'req_log_queue_depth');
my $after_req_bytes_max = stat_value($after, 'req_log_queue_bytes_max');
my $after_drops = stat_value($after, 'log_dropped_messages');
my $after_req_drops = stat_value($after, 'req_log_dropped_messages');

cmp_ok($after_generic_enqueues, '>=', $before_generic_enqueues + 4, 'generic queue enqueues increase for payload burst logs');
is($after_generic_full, $before_generic_full, 'generic queue does not need to go full during the small payload burst');
is($after_generic_depth, 0, 'generic queue drains back to zero depth');
cmp_ok($after_req_enqueues, '>=', $before_req_enqueues + 1, 'request queue enqueues increase for the long user-agent request log');
is($after_req_depth, 0, 'request queue drains back to zero depth');
cmp_ok($after_req_bytes_max, '>=', 2000, 'request queue byte high-water captures the oversized request-log line');
is($after_drops, $before_drops, 'generic log drop counter remains unchanged');
is($after_req_drops, $before_req_drops, 'request log drop counter remains unchanged');

my $log_text = slurp($server_log);
like($log_text, qr/payload burst line 4/, 'generic payload burst line reaches the configured sink');
like($log_text, qr/\[req\] A{64}/, 'request log line reaches the configured sink');

done_testing();
