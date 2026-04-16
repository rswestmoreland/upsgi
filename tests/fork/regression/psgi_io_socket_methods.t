use strict;
use warnings;

use File::Spec;
use FindBin;
use Socket qw(AF_INET SOCK_STREAM IPPROTO_TCP);
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root http_get pick_port render_profile start_server stop_server wait_http);

sub append_config_lines {
    my ($path, @lines) = @_;
    open my $fh, '>>', $path or die "unable to append to $path: $!\n";
    print {$fh} "\n";
    for my $line (@lines) {
        $line =~ s/^\s+//;
        $line =~ s/\s+$//;
        next unless length $line;
        $line =~ s/\s*=\s*/: /;
        print {$fh} "  $line\n";
    }
    close $fh;
}

my $binary = default_binary();
my $app = fixture_app('app_psgi_io_semantics.psgi');
my $static_root = fixture_static_root();
my $artifact_dir = build_artifact_dir('psgi_io_socket_methods');
my $config = File::Spec->catfile($artifact_dir, 'baseline.yaml');
my $log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(33);

render_profile(
    profile => 'baseline',
    output_yaml => $config,
    app => $app,
    static_root => $static_root,
    log_file => $log,
    port => $port,
);
append_config_lines(
    $config,
    "psgi-enable-psgix-io = true\n",
);

start_server(
    binary => $binary,
    config_yaml => $config,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/io-methods'), 'psgix.io socket-method server becomes reachable');
my $res = http_get(host => '127.0.0.1', port => $port, path => '/io-methods');
is($res->{status}, 200, 'socket-method request returns 200');
like($res->{content}, qr/^has_io=1$/m, 'psgix.io is present when enabled');
like($res->{content}, qr/^connected_len=\d+$/m, 'connected returns a packed peer address');
like($res->{content}, qr/^peername_len=\d+$/m, 'peername returns a packed peer address');
like($res->{content}, qr/^sockname_len=\d+$/m, 'sockname returns a packed local address');
like($res->{content}, qr/^sockdomain=\Q@{[AF_INET]}\E$/m, 'sockdomain reports AF_INET');
like($res->{content}, qr/^socktype=\Q@{[SOCK_STREAM]}\E$/m, 'socktype reports SOCK_STREAM');
like($res->{content}, qr/^protocol=(?:\Q@{[IPPROTO_TCP]}\E|NA)$/m, 'protocol reports TCP when available');

done_testing();
