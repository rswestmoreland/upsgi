use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('static');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(4);

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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/assets/hello.txt'), 'static profile becomes reachable');

my $static_resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/hello.txt');
is($static_resp->{status}, 200, 'static mapped file returns 200');
like($static_resp->{content}, qr/hello from upsgi static map/, 'static mapped file body is returned');

my $nested_resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/nested/index.js');
is($nested_resp->{status}, 200, 'nested static mapped file returns 200');
like($nested_resp->{content}, qr/upsgi static nested ok/, 'nested static mapped file body is returned');

my $fallback_resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/missing.txt');
is($fallback_resp->{status}, 200, 'missing static file falls back to PSGI app');
like($fallback_resp->{content}, qr/upsgi simple ok/, 'missing static file reaches PSGI fallback app');

done_testing();
