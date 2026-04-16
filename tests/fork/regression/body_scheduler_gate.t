use strict;
use warnings;

use File::Spec;
use FindBin;
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
    ) or die "unable to connect to stats socket on port $port: $!
";

    my $json = '';
    while (1) {
        my $buf = '';
        my $read = sysread($sock, $buf, 4096);
        last if !defined($read) || $read == 0;
        $json .= $buf;
    }
    close $sock;

    my %values;
    for my $key (qw(body_sched_no_credit_skips body_sched_disabled_fallbacks body_sched_bytes_total body_sched_completed_items body_sched_promotions_to_bulk body_sched_full_budget_turns body_sched_wait_relief_events body_sched_yield_hints)) {
        my @matches = ($json =~ /"$key":\s*([0-9]+)/g);
        $values{worker}{$key} = $matches[0] // 0;
        $values{core}{$key} = $matches[-1] // 0;
    }

    return \%values;
}

sub run_case {
    my (%args) = @_;
    my $binary = default_binary();
    my $artifact_dir = build_artifact_dir($args{name});
    my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
    my $port = pick_port($args{port_offset});
    my $stats_port = pick_port($args{port_offset} + 1);
    my $body = ('B' x (320 * 1024));

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
        "stats = 127.0.0.1:$stats_port
",
        ($args{disable_scheduler} ? 'disable-body-scheduler = true
' : ()),
    );

    start_server(
        binary => $binary,
        config_yaml => $config_yaml,
        artifact_dir => $artifact_dir,
    );

    eval {
        ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), "$args{name} profile becomes reachable");

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

        is($resp->{status}, 200, "$args{name} upload returns 200");

        my $after = fetch_stats($stats_port);
        cmp_ok($after->{core}{body_sched_bytes_total}, '>=', $before->{core}{body_sched_bytes_total} + length($body), "$args{name} records uploaded bytes");
        cmp_ok($after->{core}{body_sched_completed_items}, '>=', $before->{core}{body_sched_completed_items} + 1, "$args{name} records completion");
        cmp_ok($after->{core}{body_sched_promotions_to_bulk}, '>=', $before->{core}{body_sched_promotions_to_bulk} + 1, "$args{name} still promotes the larger upload to bulk");

        if (!$args{disable_scheduler}) {
            is($after->{core}{body_sched_disabled_fallbacks}, $before->{core}{body_sched_disabled_fallbacks}, "$args{name} avoids disabled fallback accounting when scheduler is enabled");
            cmp_ok($after->{core}{body_sched_no_credit_skips}, '>', $before->{core}{body_sched_no_credit_skips}, "$args{name} records capped-budget scheduler turns");
            cmp_ok($after->{core}{body_sched_full_budget_turns}, '>=', $before->{core}{body_sched_full_budget_turns}, "$args{name} keeps full-budget telemetry monotonic when the scheduler is enabled");
        }
        else {
            cmp_ok($after->{core}{body_sched_disabled_fallbacks}, '>=', $before->{core}{body_sched_disabled_fallbacks} + 1, "$args{name} records disabled fallback usage by default");
        }
    };

    my $err = $@;
    stop_server($artifact_dir);
    die $err if $err;
}

run_case(
    name => 'body_scheduler_disabled',
    port_offset => 240,
    disable_scheduler => 1,
);

run_case(
    name => 'body_scheduler_default_on',
    port_offset => 250,
);

done_testing();
