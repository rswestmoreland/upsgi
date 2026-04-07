use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_static_root render_profile slurp);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('fault_bad_app_path');
my $config_yaml = File::Spec->catfile($artifact_dir, 'bad_app.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(8);
my $missing_app = File::Spec->catfile($artifact_dir, 'missing_app.psgi');

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => $missing_app,
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

local @ENV{qw(UPSGI_BIN UPSGI_TEST_PORT UPSGI_TEST_PORT_BASE UPTEST_BIN UPTEST_PORT UPTEST_PORT_BASE)};

my $cmd = sprintf("%s --config %s > %s 2>&1", quotemeta($binary), quotemeta($config_yaml), quotemeta($server_log));
my $rc = system('sh', '-c', $cmd);
$rc = $rc >> 8;

ok($rc != 0, 'server exits nonzero for a missing PSGI app path');
my $log_text = slurp($server_log);
like($log_text, qr/No such file|unable to find PSGI function entry point|unable to load|realpath/, 'failure log reports a missing-app style failure');
like($log_text, qr/GAME OVER|no app loaded|No such file|unable to find PSGI function entry point/, 'failure log reports the app could not be loaded');

done_testing();
