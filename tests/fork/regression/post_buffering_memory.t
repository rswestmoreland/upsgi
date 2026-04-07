use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app fixture_static_root http_request pick_port render_profile slurp start_server stop_server wait_http);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('post_buffering_memory');
my $config_yaml = File::Spec->catfile($artifact_dir, 'post_buffering.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(9);
my $mem_payload = ('abcd' x 5000);   # 20 KB, larger than bufsize and smaller than threshold
my $disk_payload = ('wxyz' x 20000); # 80 KB, larger than threshold

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_upload.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "post-buffering = 65536\n",
    "post-buffering-bufsize = 4096\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/upload'), 'post-buffering profile becomes reachable');

my $mem_resp = http_request(
    method => 'POST',
    host => '127.0.0.1',
    port => $port,
    path => '/upload',
    headers => {
        'Content-Type' => 'text/plain',
        'Content-Length' => length($mem_payload),
    },
    content => $mem_payload,
    timeout => 10,
);

is($mem_resp->{status}, 200, 'buffered in-memory upload returns 200');
is($mem_resp->{headers}{'x-body-length'}, length($mem_payload), 'in-memory upload length is reported correctly');
is($mem_resp->{content}, $mem_payload, 'in-memory upload body is delivered intact when payload exceeds bufsize');

my $disk_resp = http_request(
    method => 'POST',
    host => '127.0.0.1',
    port => $port,
    path => '/upload',
    headers => {
        'Content-Type' => 'text/plain',
        'Content-Length' => length($disk_payload),
    },
    content => $disk_payload,
    timeout => 10,
);

is($disk_resp->{status}, 200, 'disk-spilled upload returns 200');
is($disk_resp->{headers}{'x-body-length'}, length($disk_payload), 'disk-spilled upload length is reported correctly');
is($disk_resp->{content}, $disk_payload, 'disk-spilled upload body is delivered intact');

my $log = slurp($server_log);
unlike($log, qr/setting request body buffering size to 65536 bytes/, 'server does not clamp chunk buffer to spill threshold');

stop_server($artifact_dir);

$artifact_dir = undef;

done_testing();
