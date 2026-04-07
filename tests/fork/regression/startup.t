use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile repo_root slurp start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('startup');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(0);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_simple.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'server becomes reachable');
my $resp = http_get(host => '127.0.0.1', port => $port, path => '/');

is($resp->{status}, 200, 'startup request returns 200');
like($resp->{content}, qr/upsgi simple ok/, 'startup request reaches PSGI app');
is($resp->{headers}{'x-upsgi-app'}, 'simple', 'response headers identify the fixture app');

my $launch_cmd = slurp(File::Spec->catfile($artifact_dir, 'launch.cmd'));
like($launch_cmd, qr/--config/, 'artifact launch command captured');

my $meta = slurp(File::Spec->catfile($artifact_dir, 'meta.env'));
like($meta, qr/UPSIG_PID=\d+/, 'artifact metadata captured pid');

done_testing();
