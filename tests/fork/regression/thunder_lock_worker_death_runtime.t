use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;
use Time::HiRes qw(sleep);

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fetch_stats_json fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http slurp);

sub read_pid_file {
    my ($path) = @_;
    open my $fh, '<', $path or die "open $path: $!\n";
    my $pid = <$fh>;
    close $fh;
    chomp $pid;
    return $pid;
}

sub child_pids_of {
    my ($ppid) = @_;
    my @pids = grep { length } split /\s+/, qx(ps -o pid= --ppid $ppid 2>/dev/null);
    return @pids;
}

sub wait_for_respawn {
    my (%args) = @_;
    my $master_pid = $args{master_pid};
    my $victim_pid = $args{victim_pid};
    my $expected_workers = $args{expected_workers} || 2;
    my $attempts = $args{attempts} || 80;
    my $sleep_seconds = defined $args{sleep_seconds} ? $args{sleep_seconds} : 0.10;

    for (1 .. $attempts) {
        my @children = child_pids_of($master_pid);
        my %seen = map { $_ => 1 } @children;
        if (@children >= $expected_workers && !$seen{$victim_pid}) {
            return @children;
        }
        sleep $sleep_seconds;
    }

    return;
}

sub wait_for_log_pattern {
    my ($path, $pattern, $attempts, $sleep_seconds) = @_;
    $attempts ||= 100;
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

sub sum_worker_stat {
    my ($stats, $key) = @_;
    my $sum = 0;
    for my $worker (@{$stats->{workers} || []}) {
        $sum += $worker->{$key} || 0;
    }
    return $sum;
}

sub run_backend_case {
    my (%args) = @_;
    my $name = $args{name};
    my $backend_opt = $args{backend_opt};
    my $backend_log = $args{backend_log};

    my $binary = default_binary();
    my $artifact_dir = build_artifact_dir("thunder_lock_worker_death_$name");
    my $config_yaml = File::Spec->catfile($artifact_dir, 'server.yaml');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
    my $pid_file = File::Spec->catfile($artifact_dir, 'server.pid');
    my $port = pick_port($args{port_offset});
    my $stats_port = pick_port($args{port_offset} + 1);

    render_profile(
        profile => 'baseline',
        output_yaml => $config_yaml,
        app => fixture_app('app_simple.psgi'),
        static_root => fixture_static_root(),
        log_file => $server_log,
        port => $port,
    );

    open my $append_fh, '>>', $config_yaml or die "unable to append worker-death config: $!\n";
    print {$append_fh} "\n  workers: 2\n";
    print {$append_fh} "  thunder-lock: true\n";
    print {$append_fh} "  stats: 127.0.0.1:$stats_port\n";
    print {$append_fh} "  thunder-lock-backend: $backend_opt\n" if defined $backend_opt;
    close $append_fh;

    start_server(
        binary => $binary,
        config_yaml => $config_yaml,
        artifact_dir => $artifact_dir,
    );

    my $stopped = 0;
    eval {
        ok(wait_http(host => '127.0.0.1', port => $port, path => '/alive'), "$name listener becomes reachable");
        ok(wait_for_log_pattern($server_log, qr/\Q$backend_log\E/, 120, 0.10), "$name startup log reports expected thunder-lock backend");

        my $master_pid = read_pid_file($pid_file);
        my @before_children = child_pids_of($master_pid);
        is(scalar @before_children, 2, "$name starts with two worker processes");

        my $before_stats = fetch_stats_json(port => $stats_port);
        cmp_ok(sum_worker_stat($before_stats, 'thunder_lock_acquires'), '>=', 0, "$name stats socket is readable before worker death");

        my $victim_pid = $before_children[0];
        ok(kill(9, $victim_pid), "$name can SIGKILL one worker");

        my @after_children = wait_for_respawn(
            master_pid => $master_pid,
            victim_pid => $victim_pid,
            expected_workers => 2,
            attempts => 100,
            sleep_seconds => 0.10,
        );
        ok(@after_children, "$name respawns the killed worker");
        isnt(join(' ', sort @after_children), join(' ', sort @before_children), "$name worker pid set changes after respawn");
        ok(!(grep { $_ == $victim_pid } @after_children), "$name dead worker pid disappears from the child set");
        ok(wait_for_log_pattern($server_log, qr/Respawned upsgi worker/, 120, 0.10), "$name master logs worker respawn");

        for my $idx (1 .. 4) {
            my $resp = http_get(host => '127.0.0.1', port => $port, path => "/after-death-$idx");
            is($resp->{status}, 200, "$name request $idx succeeds after respawn");
        }

        my $after_stats = fetch_stats_json(port => $stats_port);
        is(scalar(@{$after_stats->{workers} || []}), 2, "$name stats still report two worker entries after respawn");
        cmp_ok(sum_worker_stat($after_stats, 'thunder_lock_acquires'), '>=', sum_worker_stat($before_stats, 'thunder_lock_acquires'), "$name thunder-lock acquisition accounting remains readable after respawn");
    };
    my $err = $@;
    stop_server($artifact_dir);
    $stopped = 1;
    die $err if $err;
}

run_backend_case(
    name => 'robust_auto',
    port_offset => 520,
    backend_log => 'thunder lock backend: robust-pthread',
);

run_backend_case(
    name => 'fdlock_forced',
    port_offset => 530,
    backend_opt => 'fdlock',
    backend_log => 'thunder lock backend: fd-lock',
);

done_testing();
