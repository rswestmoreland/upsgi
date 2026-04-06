use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http slurp);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('psgi_body_types');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(52);
my $app = fixture_app('app_psgi_body_types.psgi');
my $code = slurp($app);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => $app,
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/Array'), 'body-types server becomes reachable');

for my $case (
    [ 'Array',      'ARRAY'      ],
    [ 'DATA',       'GLOB'       ],
    [ 'FILEHANDLE', 'GLOB'       ],
    [ 'FileHandle', 'FileHandle' ],
    [ 'IO::File',   'IO::File'   ],
    [ 'IO::String', 'IO::String' ],
    [ 'ObjectPath', 'ObjectPath' ],
) {
    my ($path, $ref) = @$case;
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/' . $path);
    is($resp->{status}, 200, "$path returns 200");
    is($resp->{headers}{'x-ref'}, $ref, "$path reports the expected body ref type");
    is($resp->{content}, $code, "$path produces the expected body content");
}

for my $path (qw(
    Code
    DIRHANDLE
    Float
    FloatRef
    Format
    FormatRef
    Hash
    Int
    IntRef
    Object
    Regexp
    String
    StringRef
    Undef
    UndefRef
)) {
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/' . $path);
    ok($resp->{status} >= 500 || !$resp->{success}, "$path does not return a normal success response");
}

my $log_text = slurp($server_log);
like($log_text, qr/invalid PSGI response body/, 'log records invalid body rejections for unsupported body types');

done_testing();
