use strict;
use warnings;
use File::Spec;
use FindBin;
use Test::More;
use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app fixture_static_root pick_port render_profile start_server stop_server wait_http http_get);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('observability_profile_script');
my $config_yaml = File::Spec->catfile($artifact_dir, 'observability.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(250);
my $stats_port = pick_port(251);
my $script = File::Spec->catfile($FindBin::Bin, '..', '..', '..', 'tools', 'upsgi_profile_stats.py');

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_simple.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "stats = 127.0.0.1:$stats_port\n",
    "stats-http = true\n",
);

start_server(binary => $binary, config_yaml => $config_yaml, artifact_dir => $artifact_dir);
END { stop_server($artifact_dir) if $artifact_dir; }

ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), 'observability profile becomes reachable');
my $resp = http_get(host => '127.0.0.1', port => $port, path => '/assets/hello.txt');
is($resp->{status}, 200, 'static request succeeds before profiling snapshot');

my $cmd = sprintf('python3 %s --http http://127.0.0.1:%d/', $script, $stats_port);
my $output = `$cmd`;
my $exit_code = $? >> 8;

is($exit_code, 0, 'stats profiling helper exits successfully');
like($output, qr/^upsgi stats profile/m, 'helper prints the summary header');
like($output, qr/^logging$/m, 'helper prints the logging section');
like($output, qr/^static_path$/m, 'helper prints the static path section');
like($output, qr/^body_scheduler$/m, 'helper prints the body scheduler section');
like($output, qr/static_open_calls:\s*[1-9][0-9]*/m, 'helper reports at least one static open call after the static hit');

done_testing();
