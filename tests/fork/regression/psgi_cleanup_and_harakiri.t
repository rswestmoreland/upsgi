use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;
use Time::HiRes qw(sleep);

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile slurp start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('psgi_cleanup_harakiri');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(53);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => fixture_app('app_psgi_cleanup_harakiri.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

start_server(
    binary => $binary,
    config_ini => $config_ini,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/pid'), 'cleanup/harakiri server becomes reachable');

my $cleanup = http_get(host => '127.0.0.1', port => $port, path => '/cleanup');
is($cleanup->{status}, 200, 'cleanup request returns 200');
like($cleanup->{content}, qr/^cleanup=queued$/m, 'cleanup request confirms handlers were queued');
my ($cleanup_pid) = $cleanup->{content} =~ /^pid=(\d+)$/m;
ok($cleanup_pid, 'cleanup request reports a worker pid');
sleep 0.3;
my $cleanup_log = slurp($server_log);
like($cleanup_log, qr/\Q$cleanup_pid: cleanup one\E/, 'cleanup handler one ran');
like($cleanup_log, qr/\Q$cleanup_pid: cleanup two\E/, 'cleanup handler two ran');

my $before = http_get(host => '127.0.0.1', port => $port, path => '/pid');
is($before->{status}, 200, 'pid request before harakiri returns 200');
my ($before_pid) = $before->{content} =~ /^pid=(\d+)$/m;
ok($before_pid, 'pid request before harakiri reports a worker pid');

my $harakiri = http_get(host => '127.0.0.1', port => $port, path => '/harakiri');
is($harakiri->{status}, 200, 'harakiri request returns 200');
like($harakiri->{content}, qr/^commit=1$/m, 'harakiri request commits psgix.harakiri');
my ($harakiri_pid) = $harakiri->{content} =~ /^pid=(\d+)$/m;
ok($harakiri_pid, 'harakiri request reports the dying worker pid');

my $after_pid;
for (1 .. 40) {
    sleep 0.10;
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/pid');
    next unless $resp->{status} == 200;
    ($after_pid) = $resp->{content} =~ /^pid=(\d+)$/m;
    last if $after_pid && $after_pid ne $harakiri_pid;
}
ok($after_pid, 'pid request after harakiri reports a worker pid');
isnt($after_pid, $harakiri_pid, 'worker pid changes after psgix.harakiri.commit');

my $after_log = slurp($server_log);
like($after_log, qr/\*\*\* psgix\.harakiri\.commit requested \*\*\*/, 'log records psgix.harakiri.commit');
like($after_log, qr/\Q$harakiri_pid: harakiri destroy\E/, 'destroy hook ran for harakiri object');

done_testing();
