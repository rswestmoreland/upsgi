use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile repo_root run_ok slurp start_server stop_server wait_http helper_path);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('compatibility');
my $config_yaml = File::Spec->catfile($artifact_dir, 'legacy.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(1);

render_profile(
    profile => 'legacy',
    output_yaml => $config_yaml,
    app => fixture_app('app_simple.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

my $config_text = slurp($config_yaml);
like($config_text, qr/^\s{2}http-modifier1:\s*5$/m, 'legacy profile keeps http-modifier1');
like($config_text, qr/^\s{2}http-socket-modifier1:\s*5$/m, 'legacy profile keeps http-socket-modifier1');
like($config_text, qr/^\s{2}perl-no-die-catch:\s*true$/m, 'legacy profile keeps perl-no-die-catch');

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/compat'), 'legacy compatibility profile starts');
my $resp = http_get(host => '127.0.0.1', port => $port, path => '/compat');

is($resp->{status}, 200, 'compatibility profile returns 200');
like($resp->{content}, qr/path=\/compat/, 'compatibility profile still reaches PSGI app');
is($resp->{headers}{'x-request-method'}, 'GET', 'request method handling is unchanged');

my $assert_not = File::Spec->catfile(repo_root(), 'tests', 'fork', 'helpers', 'assert_log_not_contains.sh');
ok(run_ok($assert_not, $server_log, 'unknown option'), 'legacy compatibility options do not trigger unknown-option failures');

done_testing();
