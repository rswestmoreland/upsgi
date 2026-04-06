use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port reload_server render_profile start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('reload');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(7);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => fixture_app('app_simple.psgi'),
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/reload-before'), 'reload profile becomes reachable before reload');
my $before = http_get(host => '127.0.0.1', port => $port, path => '/reload-before');
is($before->{status}, 200, 'request before reload returns 200');

ok(reload_server($artifact_dir), 'master reload signal is accepted');
ok(wait_http(host => '127.0.0.1', port => $port, path => '/reload-after', attempts => 120, sleep_seconds => 0.10), 'reload profile becomes reachable after reload');

my $after = http_get(host => '127.0.0.1', port => $port, path => '/reload-after');
is($after->{status}, 200, 'request after reload returns 200');
like($after->{content}, qr/path=\/reload-after/, 'request after reload still reaches PSGI app');

done_testing();
