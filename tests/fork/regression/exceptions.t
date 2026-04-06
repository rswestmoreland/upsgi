use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile repo_root run_ok start_server stop_server wait_http);

my $binary = default_binary();
my $assert_contains = File::Spec->catfile(repo_root(), 'tests', 'fork', 'helpers', 'assert_log_contains.sh');
my $assert_not = File::Spec->catfile(repo_root(), 'tests', 'fork', 'helpers', 'assert_log_not_contains.sh');

sub run_case {
    my (%args) = @_;
    my $artifact_dir = build_artifact_dir($args{name});
    my $config_ini = File::Spec->catfile($artifact_dir, $args{profile} . '.ini');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
    my $port = $args{port};

    render_profile(
        profile => $args{profile},
        output_ini => $config_ini,
        app => fixture_app('app_die.psgi'),
        static_root => fixture_static_root(),
        log_file => $server_log,
        port => $port,
    );

    start_server(
        binary => $binary,
        config_ini => $config_ini,
        artifact_dir => $artifact_dir,
    );

    ok(wait_http(host => '127.0.0.1', port => $port, path => '/'), "$args{name} server becomes reachable");
    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/boom');
    cmp_ok($resp->{status}, '>=', 500, "$args{name} boom path returns an error");

    return ($artifact_dir, $server_log);
}

my ($baseline_artifact, $baseline_log) = run_case(
    name => 'exceptions_baseline',
    profile => 'baseline',
    port => pick_port(2),
);
ok(run_ok($assert_not, $baseline_log, 'Trace begun'), 'baseline profile does not emit a Perl stack trace marker by default');
stop_server($baseline_artifact);

my ($debug_artifact, $debug_log) = run_case(
    name => 'exceptions_debug',
    profile => 'debug_exceptions',
    port => pick_port(3),
);
ok(
    run_ok($assert_contains, $debug_log, 'Devel::StackTrace')
        || run_ok($assert_contains, $debug_log, 'Trace begun')
        || run_ok($assert_contains, $debug_log, '[uwsgi-perl error] upsgi regression die marker'),
    'debug exception profile emits exception-visibility logging signals',
);
stop_server($debug_artifact);

done_testing();
