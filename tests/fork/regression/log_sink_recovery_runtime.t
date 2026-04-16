use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;
use IO::Socket::INET;
use IO::Socket::UNIX;
use POSIX qw(_exit);

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(append_yaml_options pick_port build_artifact_dir default_binary fixture_app fixture_static_root http_get render_profile start_server stop_server wait_http slurp);

sub fetch_stats {
    my ($port) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $port,
        Proto => 'tcp',
        Timeout => 5,
    ) or die "unable to connect to stats socket on port $port: $!\n";
    my $json = '';
    while (1) {
        my $buf = '';
        my $read = sysread($sock, $buf, 4096);
        last if !defined($read) || $read == 0;
        $json .= $buf;
    }
    close $sock;
    return $json;
}

sub stat_value {
    my ($json, $key) = @_;
    return 0 unless $json =~ /"\Q$key\E"\s*:\s*(\d+)/;
    return $1 + 0;
}

sub start_unix_dgram_sink {
    my ($path, $outfile) = @_;
    unlink $path;
    my $pid = fork();
    die "fork failed: $!\n" unless defined $pid;
    if ($pid == 0) {
        my $sock = IO::Socket::UNIX->new(
            Type => SOCK_DGRAM,
            Local => $path,
        ) or die "unable to bind unix datagram sink $path: $!\n";
        open my $fh, '>>', $outfile or die "open $outfile: $!\n";
        while (1) {
            my $buf = '';
            my $peer = $sock->recv($buf, 65535);
            last if !defined $peer;
            next unless length $buf;
            print {$fh} $buf;
            $fh->flush();
        }
        close $fh;
        close $sock;
        _exit(0);
    }
    return $pid;
}

sub stop_unix_dgram_sink {
    my ($pid, $path) = @_;
    return unless $pid;
    kill 'TERM', $pid;
    waitpid($pid, 0);
    unlink $path if $path;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('log_sink_recovery_runtime');
my $config_yaml = File::Spec->catfile($artifact_dir, 'recovery.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $sink_out = File::Spec->catfile($artifact_dir, 'sink.log');
my $status_out = File::Spec->catfile($artifact_dir, 'status.txt');
my $port = pick_port(76);
my $stats_port = pick_port(77);
my $sink_path = sprintf('/tmp/upsgi_lr_%d.sock', $$);
my $sink_pid = start_unix_dgram_sink($sink_path, $sink_out);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_log_burst.psgi'),
    static_root => fixture_static_root(),
    log_file => $server_log,
    port => $port,
);

append_yaml_options(
    $config_yaml,
    "log-master = true\n",
    "log-drain-burst = 1\n",
    "logger = socket:$sink_path\n",
    "stats = 127.0.0.1:$stats_port\n",
);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_unix_dgram_sink($sink_pid, $sink_path) if $sink_pid;
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/burst?n=1'), 'sink recovery profile becomes reachable');
my $before = fetch_stats($stats_port);
my $before_attempts = stat_value($before, 'log_sink_recovery_attempts');
my $before_successes = stat_value($before, 'log_sink_recovery_successes');

stop_unix_dgram_sink($sink_pid, $sink_path);
undef $sink_pid;

my $child = fork();
die "fork failed: $!\n" unless defined $child;
if ($child == 0) {
    my $resp = http_get(
        host => '127.0.0.1',
        port => $port,
        path => '/burst?n=1',
        timeout => 20,
    );
    open my $fh, '>', $status_out or die "open $status_out: $!\n";
    print {$fh} ($resp->{status} || 0);
    close $fh;
    _exit(0);
}

select undef, undef, undef, 0.50;
$sink_pid = start_unix_dgram_sink($sink_path, $sink_out);
waitpid($child, 0);

my $resp_recovered = http_get(
    host => '127.0.0.1',
    port => $port,
    path => '/burst?n=2',
    timeout => 20,
);

is($resp_recovered->{status}, 200, 'request returns 200 after the socket sink is restored');
select undef, undef, undef, 0.75;

open my $sfh, '<', $status_out or die "open $status_out: $!\n";
my $missing_status = <$sfh>;
close $sfh;
chomp $missing_status;
is($missing_status + 0, 200, 'request returns 200 while the socket sink is absent and later restored');

my $after = fetch_stats($stats_port);
my $after_attempts = stat_value($after, 'log_sink_recovery_attempts');
my $after_successes = stat_value($after, 'log_sink_recovery_successes');
cmp_ok($after_attempts, '>=', $before_attempts + 1, 'recovery attempts increase when the socket sink goes away');
cmp_ok($after_successes, '>=', $before_successes + 1, 'recovery successes increase after the socket sink is restored');

my $sink_text = slurp($sink_out);
like($sink_text, qr/burst line 2/, 'restored socket sink receives log records after recovery');

done_testing();
