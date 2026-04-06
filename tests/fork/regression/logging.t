use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile slurp start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('logging');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(5);
my $xff = '203.0.113.10';

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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/logging'), 'logging profile becomes reachable');
my $resp = http_get(
    host => '127.0.0.1',
    port => $port,
    path => '/logging',
    headers => {
        'X-Forwarded-For' => $xff,
    },
);

is($resp->{status}, 200, 'logging request returns 200');
like($resp->{content}, qr/path=\/logging/, 'logging request reaches PSGI app');

select undef, undef, undef, 0.25;
my $log_text = slurp($server_log);
like($log_text, qr/\Q$xff\E/, 'request log captures the forwarded-for address');
like($log_text, qr/GET \/logging/, 'request log captures the request line');

done_testing();
