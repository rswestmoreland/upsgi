use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;
use Time::HiRes qw(sleep);

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fetch_stats_json fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http slurp);

sub sum_worker_stat {
    my ($stats, $key) = @_;
    my $sum = 0;
    for my $worker (@{$stats->{workers} || []}) {
        $sum += $worker->{$key} || 0;
    }
    return $sum;
}

sub wait_for_log_pattern {
    my ($path, $pattern, $attempts, $sleep_seconds) = @_;
    $attempts ||= 80;
    $sleep_seconds ||= 0.10;

    for (1 .. $attempts) {
        my $text = eval { slurp($path) };
        if (defined $text && $text =~ $pattern) {
            return 1;
        }
        sleep $sleep_seconds;
    }

    return 0;
}

my $binary = default_binary();

{
    my $artifact_dir = build_artifact_dir('thunder_lock_observability_mapped');
    my $config_yaml = File::Spec->catfile($artifact_dir, 'mapped.yaml');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
    my $port_a = pick_port(320);
    my $port_b = pick_port(321);
    my $stats_port = pick_port(322);

    render_profile(
        profile => 'baseline',
        output_yaml => $config_yaml,
        app => fixture_app('app_simple.psgi'),
        static_root => fixture_static_root(),
        log_file => $server_log,
        port => $port_a,
    );

    open my $append_fh, '>>', $config_yaml or die "unable to append mapped config: $!\n";
    print {$append_fh} "\n  workers: 2\n";
    print {$append_fh} "  thunder-lock: true\n";
    print {$append_fh} "  stats: 127.0.0.1:$stats_port\n";
    print {$append_fh} "  http-socket: 127.0.0.1:$port_b\n";
    print {$append_fh} "  map-socket: 0:1\n";
    print {$append_fh} "  map-socket: 1:2\n";
    close $append_fh;

    start_server(
        binary => $binary,
        config_yaml => $config_yaml,
        artifact_dir => $artifact_dir,
    );

    ok(wait_http(host => '127.0.0.1', port => $port_a, path => '/mapped-a'), 'mapped socket A becomes reachable');
    ok(wait_http(host => '127.0.0.1', port => $port_b, path => '/mapped-b'), 'mapped socket B becomes reachable');
    ok(wait_for_log_pattern($server_log, qr/accept mode: worker-local mapped sockets \(thunder lock bypassed\)/, 120, 0.10), 'mapped sockets log the thunder-lock bypass mode');

    my $before = fetch_stats_json(port => $stats_port);

    my $resp_a = http_get(host => '127.0.0.1', port => $port_a, path => '/mapped-a');
    my $resp_b = http_get(host => '127.0.0.1', port => $port_b, path => '/mapped-b');
    is($resp_a->{status}, 200, 'mapped socket A request returns 200');
    is($resp_b->{status}, 200, 'mapped socket B request returns 200');

    my $after = fetch_stats_json(port => $stats_port);

    cmp_ok(sum_worker_stat($after, 'thunder_lock_bypass_count'), '>', sum_worker_stat($before, 'thunder_lock_bypass_count'), 'mapped worker-local sockets increase the thunder-lock bypass counter');
    is(sum_worker_stat($after, 'thunder_lock_acquires'), sum_worker_stat($before, 'thunder_lock_acquires'), 'mapped worker-local sockets avoid process-shared thunder-lock acquisitions');

    stop_server($artifact_dir);
}

{
    my $artifact_dir = build_artifact_dir('thunder_lock_observability_shared');
    my $config_yaml = File::Spec->catfile($artifact_dir, 'shared.yaml');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
    my $port = pick_port(323);
    my $stats_port = pick_port(324);

    render_profile(
        profile => 'baseline',
        output_yaml => $config_yaml,
        app => fixture_app('app_simple.psgi'),
        static_root => fixture_static_root(),
        log_file => $server_log,
        port => $port,
    );

    open my $append_fh, '>>', $config_yaml or die "unable to append shared config: $!\n";
    print {$append_fh} "\n  workers: 2\n";
    print {$append_fh} "  thunder-lock: true\n";
    print {$append_fh} "  stats: 127.0.0.1:$stats_port\n";
    close $append_fh;

    start_server(
        binary => $binary,
        config_yaml => $config_yaml,
        artifact_dir => $artifact_dir,
    );

    ok(wait_http(host => '127.0.0.1', port => $port, path => '/shared'), 'shared-listener profile becomes reachable');
    ok(wait_for_log_pattern($server_log, qr/accept mode: shared listeners serialized by thunder lock/, 120, 0.10), 'shared listeners log the serialized thunder-lock mode');

    my $before = fetch_stats_json(port => $stats_port);

    for my $idx (1 .. 6) {
        my $resp = http_get(host => '127.0.0.1', port => $port, path => "/shared-$idx");
        is($resp->{status}, 200, "shared listener request $idx returns 200");
    }

    my $after = fetch_stats_json(port => $stats_port);

    cmp_ok(sum_worker_stat($after, 'thunder_lock_acquires'), '>', sum_worker_stat($before, 'thunder_lock_acquires'), 'shared listeners record thunder-lock acquisitions');
    cmp_ok(sum_worker_stat($after, 'thunder_lock_hold_us'), '>', sum_worker_stat($before, 'thunder_lock_hold_us'), 'shared listeners accumulate thunder-lock hold time');
    cmp_ok(sum_worker_stat($after, 'thunder_lock_wait_us'), '>=', sum_worker_stat($before, 'thunder_lock_wait_us'), 'shared listeners expose monotonic thunder-lock wait accounting');
    is(sum_worker_stat($after, 'thunder_lock_bypass_count'), sum_worker_stat($before, 'thunder_lock_bypass_count'), 'shared listeners do not report bypass activity');

    stop_server($artifact_dir);
}


done_testing();
