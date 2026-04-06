use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http);

sub append_config_lines {
    my ($path, @lines) = @_;
    open my $fh, '>>', $path or die "unable to append to $path: $!\n";
    print {$fh} "\n", @lines;
    close $fh;
}

sub parse_lines {
    my ($text) = @_;
    my %map;
    for my $line (split /\n/, $text) {
        next unless length $line;
        my ($k, $v) = split /=/, $line, 2;
        $map{$k} = defined($v) ? $v : '';
    }
    return \%map;
}

sub start_case {
    my (%args) = @_;
    my $artifact_dir = build_artifact_dir($args{name});
    my $config_ini = File::Spec->catfile($artifact_dir, 'baseline.ini');
    my $server_log = File::Spec->catfile($artifact_dir, 'server.log');

    render_profile(
        profile => 'baseline',
        output_ini => $config_ini,
        app => fixture_app('app_psgi_env_surface.psgi'),
        static_root => fixture_static_root(),
        log_file => $server_log,
        port => $args{port},
    );

    append_config_lines($config_ini, @{ $args{append} || [] }) if $args{append};

    start_server(
        binary => default_binary(),
        config_ini => $config_ini,
        artifact_dir => $artifact_dir,
    );
    ok(wait_http(host => '127.0.0.1', port => $args{port}, path => '/env'), "$args{name} server becomes reachable");
    my $resp = http_get(host => '127.0.0.1', port => $args{port}, path => '/env?mode=' . $args{name});
    is($resp->{status}, 200, "$args{name} env request returns 200");
    my $env = parse_lines($resp->{content});
    return ($artifact_dir, $env);
}

my @dirs;
END { stop_server($_) for grep { defined } @dirs; }

my ($no_master_dir, $no_master_env) = start_case(
    name => 'psgi_env_nomaster',
    port => pick_port(50),
    append => ["master = false\n"],
);
push @dirs, $no_master_dir;
is($no_master_env->{'request_method'}, 'GET', 'no-master env reports request method');
is($no_master_env->{'path_info'}, '/env', 'no-master env reports path info');
is($no_master_env->{'query_string'}, 'mode=psgi_env_nomaster', 'no-master env reports query string');
is($no_master_env->{'server_protocol'}, 'HTTP/1.1', 'no-master env reports HTTP/1.1');
is($no_master_env->{'url_scheme'}, 'http', 'no-master env reports url scheme');
is($no_master_env->{'version'}, '1.1', 'no-master env reports PSGI version 1.1');
is($no_master_env->{'has_errors'}, 1, 'no-master env exposes psgi.errors');
is($no_master_env->{'has_input'}, 1, 'no-master env exposes psgi.input');
is($no_master_env->{'has_logger'}, 1, 'no-master env exposes psgix.logger');
is($no_master_env->{'has_cleanup_handlers'}, 1, 'no-master env exposes cleanup handlers array');
is($no_master_env->{'psgi.multiprocess'}, 0, 'no-master env reports single-process');
is($no_master_env->{'psgi.multithread'}, 0, 'no-master env reports single-thread');
is($no_master_env->{'psgi.nonblocking'}, 0, 'no-master env reports blocking mode');
is($no_master_env->{'psgi.run_once'}, 0, 'no-master env reports not run-once');
is($no_master_env->{'psgi.streaming'}, 1, 'no-master env reports streaming support');
is($no_master_env->{'psgix.cleanup'}, 1, 'no-master env reports cleanup support');
is($no_master_env->{'psgix.harakiri'}, 0, 'no-master env reports harakiri disabled');
is($no_master_env->{'psgix.input.buffered'}, 0, 'no-master env reports input not buffered by default');

my ($worker_dir, $worker_env) = start_case(
    name => 'psgi_env_workers',
    port => pick_port(51),
    append => ["workers = 2\n"],
);
push @dirs, $worker_dir;
is($worker_env->{'psgi.multiprocess'}, 1, 'worker env reports multiprocess when workers > 1');
is($worker_env->{'psgix.harakiri'}, 1, 'worker env reports harakiri support under master');
is($worker_env->{'psgi.streaming'}, 1, 'worker env still reports streaming support');

done_testing();
