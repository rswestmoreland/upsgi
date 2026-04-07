use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;
use Time::HiRes qw(sleep);

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http slurp);

sub wait_for_log_pattern {
    my ($path, $pattern, $attempts, $sleep_seconds) = @_;
    $attempts ||= 80;
    $sleep_seconds ||= 0.10;

    for (1 .. $attempts) {
        my $text = eval { slurp($path) };
        if (defined $text && $text =~ $pattern) {
            return 1;
        }
        sleep $sleep_seconds;
    }

    return 0;
}

my $binary = default_binary();

{
    my $artifact_dir = build_artifact_dir('accept_path_mapped');
    my $config_yaml = File::Spec->catfile($artifact_dir, 'mapped.yaml');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
    my $port_a = pick_port(20);
    my $port_b = pick_port(21);

    render_profile(
        profile => 'baseline',
        output_yaml => $config_yaml,
        app => fixture_app('app_simple.psgi'),
        static_root => fixture_static_root(),
        log_file => $server_log,
        port => $port_a,
    );

    open my $append_fh, '>>', $config_yaml or die "unable to append mapped accept-path config: $!
";
    print {$append_fh} "\n  workers: 2
";
    print {$append_fh} "  thunder-lock: true
";
    print {$append_fh} "  http-socket: 127.0.0.1:$port_b
";
    print {$append_fh} "  map-socket: 0:1
";
    print {$append_fh} "  map-socket: 1:2
";
    close $append_fh;

    start_server(
        binary => $binary,
        config_yaml => $config_yaml,
        artifact_dir => $artifact_dir,
    );

    ok(wait_http(host => '127.0.0.1', port => $port_a, path => '/mapped-a'), 'mapped socket A becomes reachable');
    ok(wait_http(host => '127.0.0.1', port => $port_b, path => '/mapped-b'), 'mapped socket B becomes reachable');
    ok(wait_for_log_pattern($server_log, qr/accept mode: worker-local mapped sockets \(thunder lock bypassed\)/, 120, 0.10), 'worker-local mapped sockets bypass the thunder lock');

    my $resp_a = http_get(host => '127.0.0.1', port => $port_a, path => '/mapped-a');
    my $resp_b = http_get(host => '127.0.0.1', port => $port_b, path => '/mapped-b');
    is($resp_a->{status}, 200, 'mapped socket A request returns 200');
    is($resp_b->{status}, 200, 'mapped socket B request returns 200');

    stop_server($artifact_dir);
}

{
    my $artifact_dir = build_artifact_dir('accept_path_reuse_port');
    my $config_yaml = File::Spec->catfile($artifact_dir, 'reuse_port.yaml');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
    my $port = pick_port(22);

    render_profile(
        profile => 'baseline',
        output_yaml => $config_yaml,
        app => fixture_app('app_simple.psgi'),
        static_root => fixture_static_root(),
        log_file => $server_log,
        port => $port,
    );

    open my $append_fh, '>>', $config_yaml or die "unable to append reuse-port accept-path config: $!
";
    print {$append_fh} "\n  workers: 2
";
    print {$append_fh} "  thunder-lock: true
";
    print {$append_fh} "  reuse-port: true
";
    close $append_fh;

    start_server(
        binary => $binary,
        config_yaml => $config_yaml,
        artifact_dir => $artifact_dir,
    );

    ok(wait_http(host => '127.0.0.1', port => $port, path => '/reuse-port'), 'reuse-port profile becomes reachable');
    ok(wait_for_log_pattern($server_log, qr/SO_REUSEPORT on inherited listeners does not shard accepts/, 120, 0.10), 'reuse-port shared-listener guidance is logged');

    my $resp = http_get(host => '127.0.0.1', port => $port, path => '/reuse-port');
    is($resp->{status}, 200, 'reuse-port profile request returns 200');

    stop_server($artifact_dir);
}

done_testing();
