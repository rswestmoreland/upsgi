use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_request pick_port render_profile start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('upload');
my $config_yaml = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(6);
my $payload = "alpha=1\nbeta=two\n";

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_upload.psgi'),
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/upload'), 'upload profile becomes reachable');
my $resp = http_request(
    method => 'POST',
    host => '127.0.0.1',
    port => $port,
    path => '/upload',
    headers => {
        'Content-Type' => 'text/plain',
        'Content-Length' => length($payload),
    },
    content => $payload,
);

is($resp->{status}, 200, 'upload request returns 200');
is($resp->{headers}{'x-upsgi-app'}, 'upload', 'upload response identifies fixture app');
is($resp->{headers}{'x-body-length'}, length($payload), 'upload response reports the request body length');
is($resp->{content}, $payload, 'upload request body is delivered through psgi.input');

done_testing();
