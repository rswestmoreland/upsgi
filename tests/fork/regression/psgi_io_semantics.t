use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get http_request pick_port render_profile slurp start_server stop_server wait_http);

sub append_config_lines {
    my ($path, @lines) = @_;
    open my $fh, '>>', $path or die "unable to append to $path: $!\n";
    print {$fh} "\n", @lines;
    close $fh;
}

my $binary = default_binary();
my $app = fixture_app('app_psgi_io_semantics.psgi');
my $static_root = fixture_static_root();

my $artifact_dir_false = build_artifact_dir('psgi_io_false');
my $config_false = File::Spec->catfile($artifact_dir_false, 'baseline.ini');
my $log_false = File::Spec->catfile($artifact_dir_false, 'server.log');
my $port_false = pick_port(31);

render_profile(
    profile => 'baseline',
    output_ini => $config_false,
    app => $app,
    static_root => $static_root,
    log_file => $log_false,
    port => $port_false,
);

start_server(
    binary => $binary,
    config_ini => $config_false,
    artifact_dir => $artifact_dir_false,
);

ok(wait_http(host => '127.0.0.1', port => $port_false, path => '/env'), 'baseline PSGI env server becomes reachable');
my $env_false = http_get(host => '127.0.0.1', port => $port_false, path => '/env');
is($env_false->{status}, 200, 'baseline env request returns 200');
like($env_false->{content}, qr/^input_buffered=0$/m, 'baseline env exposes false psgix.input.buffered');
like($env_false->{content}, qr/^has_logger=1$/m, 'baseline env exposes psgix.logger');
like($env_false->{content}, qr/^has_errors=1$/m, 'baseline env exposes psgi.errors');
stop_server($artifact_dir_false);
$artifact_dir_false = undef;

my $artifact_dir_true = build_artifact_dir('psgi_io_true');
my $config_true = File::Spec->catfile($artifact_dir_true, 'baseline.ini');
my $log_true = File::Spec->catfile($artifact_dir_true, 'server.log');
my $port_true = pick_port(32);

render_profile(
    profile => 'baseline',
    output_ini => $config_true,
    app => $app,
    static_root => $static_root,
    log_file => $log_true,
    port => $port_true,
);
append_config_lines(
    $config_true,
    "post-buffering = 4096\n",
    "post-buffering-bufsize = 1024\n",
);

start_server(
    binary => $binary,
    config_ini => $config_true,
    artifact_dir => $artifact_dir_true,
);

END {
    stop_server($artifact_dir_true) if $artifact_dir_true;
    stop_server($artifact_dir_false) if $artifact_dir_false;
}

ok(wait_http(host => '127.0.0.1', port => $port_true, path => '/env'), 'buffered PSGI env server becomes reachable');
my $env_true = http_get(host => '127.0.0.1', port => $port_true, path => '/env');
is($env_true->{status}, 200, 'buffered env request returns 200');
like($env_true->{content}, qr/^input_buffered=1$/m, 'buffered env exposes true psgix.input.buffered');

my $err = http_get(host => '127.0.0.1', port => $port_true, path => '/error-print');
is($err->{status}, 200, 'psgi.errors print request returns 200');
like($err->{content}, qr/^rv=1$/m, 'psgi.errors print returns a true value');

my $logger_ok = http_get(host => '127.0.0.1', port => $port_true, path => '/logger-ok');
is($logger_ok->{status}, 200, 'valid psgix.logger request returns 200');
like($logger_ok->{content}, qr/^rv=1$/m, 'valid psgix.logger call returns a true value');

my $logger_bad = http_get(host => '127.0.0.1', port => $port_true, path => '/logger-bad-level');
ok($logger_bad->{status} >= 500 || !$logger_bad->{success}, 'invalid psgix.logger level does not return a normal success response');

my $payload = 'abcdef';
my $seek = http_request(
    method => 'POST',
    host => '127.0.0.1',
    port => $port_true,
    path => '/input-seek',
    headers => {
        'Content-Type' => 'text/plain',
        'Content-Length' => length($payload),
    },
    content => $payload,
);

is($seek->{status}, 200, 'buffered input seek request returns 200');
like($seek->{content}, qr/^r1=3 one=abc$/m, 'initial read returns the first bytes');
like($seek->{content}, qr/^s1=1$/m, 'seek from start reports success');
like($seek->{content}, qr/^r2=3 two=abc$/m, 'seek set rewinds to the beginning');
like($seek->{content}, qr/^s2=1$/m, 'relative seek reports success');
like($seek->{content}, qr/^r3=3 three=bcd$/m, 'seek current reads from the relative position');
like($seek->{content}, qr/^s3=1$/m, 'seek from end reports success');
like($seek->{content}, qr/^r4=2 four=ef$/m, 'seek end reads from the tail');
like($seek->{content}, qr/^s4=1$/m, 'seek reset for offset read reports success');
like($seek->{content}, qr/^r5=2 five=-ab-$/m, 'positive offset read still writes into the destination scalar');
like($seek->{content}, qr/^s5=1$/m, 'seek reset for negative offset read reports success');
like($seek->{content}, qr/^r6=2 six=wxab$/m, 'negative offset read replaces relative to the existing tail');
like($seek->{content}, qr/^s6=1$/m, 'seek reset for beyond-start negative offset read reports success');
like($seek->{content}, qr/^r7=2 seven=abAB$/m, 'negative offset read can grow and prepend before the existing scalar');

my $log_text = slurp($log_true);
like($log_text, qr/psgi\.errors smoke/, 'log captures psgi.errors output');
like($log_text, qr/\[uwsgi-perl info\] psgix logger ok/, 'log captures valid psgix.logger output');
like($log_text, qr/psgix\.logger level must be one of debug, info, warn, error, fatal/, 'log records invalid psgix.logger level usage');

done_testing();
