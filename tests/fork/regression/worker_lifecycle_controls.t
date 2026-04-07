use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;
use Time::HiRes qw(sleep time);

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http slurp);

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
my $artifact_dir = build_artifact_dir('worker_lifecycle');
my $config_yaml = File::Spec->catfile($artifact_dir, 'worker_lifecycle.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $fifo_path = File::Spec->catfile($artifact_dir, 'master.fifo');
my $port = pick_port(14);

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
    "workers = 2\n",
    "master-fifo = $fifo_path\n",
    "chain-reload-delay = 1\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/lifecycle-before'), 'worker lifecycle profile becomes reachable');
ok(-p $fifo_path, 'master fifo is created');

my $before = http_get(host => '127.0.0.1', port => $port, path => '/lifecycle-before');
is($before->{status}, 200, 'request before chain reload returns 200');

open my $fifo_fh, '>', $fifo_path or die "unable to open master fifo: $!\n";
my $started = time();
print {$fifo_fh} 'c';
close $fifo_fh;

ok(wait_for_log_pattern($server_log, qr/chain reload starting \(paced 1 sec between worker turnovers\)/, 80, 0.10), 'paced chain reload starts from master fifo command');
ok(wait_for_log_pattern($server_log, qr/chain reloading complete/, 160, 0.10), 'paced chain reload completes');
my $elapsed = time() - $started;
cmp_ok($elapsed, '>=', 1.0, 'paced chain reload takes at least the configured turnover delay');

ok(wait_http(host => '127.0.0.1', port => $port, path => '/lifecycle-after', attempts => 120, sleep_seconds => 0.10), 'worker lifecycle profile remains reachable after chain reload');
my $after = http_get(host => '127.0.0.1', port => $port, path => '/lifecycle-after');
is($after->{status}, 200, 'request after chain reload returns 200');
like($after->{content}, qr/upsgi simple ok/, 'request after chain reload still reaches PSGI app');

done_testing();
