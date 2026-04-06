use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile slurp start_server stop_server wait_http);

sub append_config_lines {
    my ($path, @lines) = @_;
    open my $fh, '>>', $path or die "unable to append to $path: $!\n";
    print {$fh} "\n", @lines;
    close $fh;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('runtime_tunable_validation');
my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(64);

render_profile(
    profile => 'baseline',
    output_ini => $config_ini,
    app => fixture_app('app_header_echo.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_config_lines(
    $config_ini,
    "master = true\n",
    "log-master = true\n",
    "log-drain-burst = 0\n",
    "chain-reload-delay = 0\n",
);

start_server(
    binary => $binary,
    config_ini => $config_ini,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/headers'), 'validation server becomes reachable');
my $resp = http_get(host => '127.0.0.1', port => $port, path => '/headers');
is($resp->{status}, 200, 'validation server still serves requests after clamping invalid tunables');

my $log = slurp($server_log);
like($log, qr/GET \/headers => generated/, 'request logging still drains when log-drain-burst is configured as zero');

done_testing();
