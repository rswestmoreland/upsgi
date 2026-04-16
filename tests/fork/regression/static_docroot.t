use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app fixture_static_root pick_port raw_upsgi_request render_profile start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('static_docroot');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $http_port = pick_port(44);
my $upsgi_port = pick_port(45);
my $static_root = fixture_static_root();

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_simple.psgi'),
    static_root => $static_root,
    log_file => $server_log,
    port => $http_port,
);

append_yaml_options(
    $config_yaml,
    'check-static-docroot = true',
    "socket = 127.0.0.1:$upsgi_port",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $http_port, path => '/assets/hello.txt'), 'static docroot profile becomes reachable');

sub raw_request {
    my ($path) = @_;
    return raw_upsgi_request(
        host => '127.0.0.1',
        port => $upsgi_port,
        vars => {
            REQUEST_METHOD => 'GET',
            REQUEST_URI => $path,
            SCRIPT_NAME => '',
            PATH_INFO => $path,
            QUERY_STRING => '',
            SERVER_NAME => 'localhost',
            SERVER_PORT => $upsgi_port,
            SERVER_PROTOCOL => 'HTTP/1.1',
            REMOTE_ADDR => '127.0.0.1',
            REMOTE_PORT => '12345',
            DOCUMENT_ROOT => $static_root,
        },
    );
}

my $static_resp = raw_request('/hello.txt');
is($static_resp->{status}, 200, 'docroot static file returns 200 over raw upsgi socket');
like($static_resp->{content}, qr/hello from upsgi static map/, 'docroot static file body is returned');

my $nested_resp = raw_request('/nested/index.js');
is($nested_resp->{status}, 200, 'docroot nested static file returns 200 over raw upsgi socket');
like($nested_resp->{content}, qr/upsgi static nested ok/, 'docroot nested static file body is returned');

my $fallback_resp = raw_request('/missing.txt');
is($fallback_resp->{status}, 200, 'missing docroot static file falls back to PSGI app over raw upsgi socket');
like($fallback_resp->{content}, qr/upsgi simple ok/, 'missing docroot static file reaches PSGI fallback app');


done_testing();
