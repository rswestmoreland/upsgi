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
my $artifact_dir = build_artifact_dir('logging_backpressure');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(40);
my $stats_port = pick_port(41);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_log_burst.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "log-master = true\n",
    "log-drain-burst = 1\n",
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/burst?n=8'), 'logging backpressure profile becomes reachable');

my $before = fetch_stats($stats_port);
my $before_log_records = stat_value($before, 'log_records');
my $before_backpressure = stat_value($before, 'log_backpressure_events');
my $before_stalls = stat_value($before, 'log_sink_stall_events');
my $before_drops = stat_value($before, 'log_dropped_messages');

my $resp = http_get(
    host => '127.0.0.1',
    port => $port,
    path => '/burst?n=8',
);

is($resp->{status}, 200, 'burst request returns 200');
like($resp->{content}, qr/burst=8/, 'burst PSGI app confirms request');

select undef, undef, undef, 0.35;

my $after = fetch_stats($stats_port);
my $after_log_records = stat_value($after, 'log_records');
my $after_backpressure = stat_value($after, 'log_backpressure_events');
my $after_stalls = stat_value($after, 'log_sink_stall_events');
my $after_drops = stat_value($after, 'log_dropped_messages');

cmp_ok($after_log_records, '>=', $before_log_records + 8, 'generic log record counter increases for the burst');
cmp_ok($after_backpressure, '>=', $before_backpressure + 1, 'backpressure counter increases when the drain burst is smaller than the log burst');
is($after_stalls, $before_stalls, 'file-backed logging does not introduce sink stall events in the burst test');
is($after_drops, $before_drops, 'file-backed logging does not drop messages in the burst test');

my $log_text = slurp($server_log);
like($log_text, qr/burst line 8/, 'burst log lines reach the configured sink');

done_testing();
