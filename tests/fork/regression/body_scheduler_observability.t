use strict;
use warnings;

use File::Spec;
use FindBin;
use JSON::PP qw(decode_json);
use IO::Socket::INET;
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

    my ($workers_blob) = $json =~ /"workers":\s*\[(.*)\]\s*\}\s*$/s;
    die "workers array not found in stats payload\n" unless defined $workers_blob;

    my %values;
    for my $key (qw(body_sched_rounds body_sched_interactive_turns body_sched_bulk_turns body_sched_requeues body_sched_promotions_to_bulk body_sched_empty_read_events body_sched_eagain_events body_sched_completed_items body_sched_bytes_interactive body_sched_bytes_bulk body_sched_bytes_total body_sched_credit_granted_bytes body_sched_credit_unused_bytes body_sched_active_items body_sched_interactive_depth_max body_sched_bulk_depth_max body_sched_residency_us_max body_sched_residency_us_p50_sample body_sched_residency_us_p95_sample body_sched_items_promoted_by_bytes body_sched_items_promoted_by_rounds body_sched_items_promoted_by_residency body_sched_near_complete_fastfinishes body_sched_overflow_protection_hits body_sched_queue_full_events body_sched_disabled_fallbacks body_sched_full_budget_turns body_sched_wait_relief_events body_sched_yield_hints)) {
        my @matches = ($json =~ /"$key":\s*([0-9]+)/g);
        $values{worker}{$key} = $matches[0] // 0;
        $values{core}{$key} = $matches[-1] // 0;
    }

    return \%values;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('body_scheduler_observability');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(210);
my $stats_port = pick_port(211);
my $body = ('A' x (320 * 1024));

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_upload.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'upload observability profile becomes reachable');

my $before = fetch_stats($stats_port);

my $resp = http_request(
    method => 'POST',
    host => '127.0.0.1',
    port => $port,
    path => '/',
    headers => {
        'Content-Type' => 'application/octet-stream',
    },
    content => $body,
    timeout => 10,
);

is($resp->{status}, 200, 'upload request returns 200');
is(length($resp->{content}), length($body), 'upload response body length matches request body length');
is($resp->{headers}{'x-body-length'}, length($body), 'upload app reports the expected body length');

my $after = fetch_stats($stats_port);

cmp_ok($after->{core}{body_sched_rounds}, '>', $before->{core}{body_sched_rounds}, 'body scheduler rounds counter increases after a body-bearing request');
cmp_ok($after->{core}{body_sched_bytes_total}, '>=', $before->{core}{body_sched_bytes_total} + length($body), 'body scheduler byte total covers the uploaded body');
cmp_ok($after->{core}{body_sched_credit_granted_bytes}, '>=', $after->{core}{body_sched_bytes_total}, 'credit granted bytes are tracked for observed reads');
cmp_ok($after->{core}{body_sched_completed_items}, '>=', $before->{core}{body_sched_completed_items} + 1, 'completed body items counter increases after a full upload');
cmp_ok($after->{core}{body_sched_promotions_to_bulk}, '>=', $before->{core}{body_sched_promotions_to_bulk} + 1, 'large enough upload is observed as a bulk promotion candidate');
cmp_ok($after->{core}{body_sched_items_promoted_by_bytes}, '>=', $before->{core}{body_sched_items_promoted_by_bytes} + 1, 'byte-threshold promotion counter increases for the larger upload');
cmp_ok($after->{core}{body_sched_interactive_turns}, '>', $before->{core}{body_sched_interactive_turns}, 'interactive lane turns are observed before promotion');
cmp_ok($after->{core}{body_sched_bulk_turns}, '>', $before->{core}{body_sched_bulk_turns}, 'bulk lane turns are observed after promotion');
cmp_ok($after->{core}{body_sched_full_budget_turns}, '>=', $before->{core}{body_sched_full_budget_turns}, 'full-budget body telemetry stays monotonic even when local read segmentation prevents a nonzero sample');
is($after->{core}{body_sched_active_items}, 0, 'active body items gauge returns to zero after request completion');
cmp_ok($after->{core}{body_sched_residency_us_max}, '>', $before->{core}{body_sched_residency_us_max}, 'residency max is updated after body completion');

is($after->{worker}{body_sched_rounds}, $after->{core}{body_sched_rounds}, 'worker aggregate rounds match the lone core');
is($after->{worker}{body_sched_bytes_total}, $after->{core}{body_sched_bytes_total}, 'worker aggregate bytes match the lone core');
is($after->{worker}{body_sched_completed_items}, $after->{core}{body_sched_completed_items}, 'worker aggregate completed items match the lone core');
is($after->{worker}{body_sched_promotions_to_bulk}, $after->{core}{body_sched_promotions_to_bulk}, 'worker aggregate promotions match the lone core');
is($after->{worker}{body_sched_active_items}, $after->{core}{body_sched_active_items}, 'worker aggregate active-items gauge matches the lone core');

done_testing();
