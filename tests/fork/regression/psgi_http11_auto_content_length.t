use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary pick_port render_profile repo_root start_server stop_server wait_http slurp);

sub rewrite_http11_profile {
    my ($path) = @_;
    my $text = slurp($path);
    $text =~ s/^  http-socket:\s*/  http11-socket: /m
        or die "failed to switch profile to http11-socket\n";
    open my $fh, '>', $path or die "unable to rewrite $path: $!\n";
    print {$fh} $text;
    close $fh;
}

sub open_socket {
    my ($port) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $port,
        Proto => 'tcp',
        Timeout => 5,
    ) or die "unable to connect to 127.0.0.1:$port: $!\n";
    return $sock;
}

sub sysread_exact {
    my ($sock, $len) = @_;
    my $buf = '';
    while (length($buf) < $len) {
        my $chunk = '';
        my $rv = sysread($sock, $chunk, $len - length($buf));
        last if !defined($rv) || $rv == 0;
        $buf .= $chunk;
    }
    return $buf;
}

sub read_http_response {
    my ($sock) = @_;
    my $raw = '';
    my $header_end = -1;
    while ($header_end < 0) {
        my $chunk = '';
        my $rv = sysread($sock, $chunk, 4096);
        die "unexpected EOF before response headers\n" if !defined($rv) || $rv == 0;
        $raw .= $chunk;
        $header_end = index($raw, "\r\n\r\n");
    }

    my $body = substr($raw, $header_end + 4);
    my @lines = split /\r\n/, substr($raw, 0, $header_end);
    my $status_line = shift @lines;
    my %headers;
    for my $line (@lines) {
        next if $line eq '';
        my ($name, $value) = split /:\s*/, $line, 2;
        next unless defined $name;
        push @{ $headers{lc $name} }, defined($value) ? $value : '';
    }

    if (exists $headers{'content-length'}) {
        my $cl = $headers{'content-length'}[0];
        my $need = $cl - length($body);
        if ($need > 0) {
            $body .= sysread_exact($sock, $need);
        }
        if (length($body) > $cl) {
            $body = substr($body, 0, $cl);
        }
    }

    return {
        status_line => $status_line,
        headers => \%headers,
        body => $body,
    };
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('psgi_http11_auto_content_length');
my $config_yaml = File::Spec->catfile($artifact_dir, 'http11.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(18);
my $a1_app = File::Spec->catfile(repo_root(), 'tests', 'fork', 'perf', 'assets', 'A1', 'app_browser_mix.psgi');
my $static_root = File::Spec->catdir(repo_root(), 'tests', 'fork', 'perf', 'assets', 'A1', 'static');

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => $a1_app,
    static_root => $static_root,
    log_file => $server_log,
    port => $port,
);
rewrite_http11_profile($config_yaml);

start_server(
    binary => $binary,
    config_yaml => $config_yaml,
    artifact_dir => $artifact_dir,
);

END {
    stop_server($artifact_dir) if $artifact_dir;
}

ok(wait_http(host => '127.0.0.1', port => $port, path => '/api/health'), 'http11 A1 fixture becomes reachable');

my $sock = open_socket($port);
print {$sock} "GET /dashboard HTTP/1.1\r\nHost: 127.0.0.1:$port\r\n\r\n";
my $first = read_http_response($sock);
like($first->{status_line}, qr{\AHTTP/1\.[01] 200\b}, 'dashboard response returns 200');
like($first->{headers}{'content-length'}[0], qr{\A\d+\z}, 'dashboard response auto-adds content length');
like($first->{headers}{'content-type'}[0], qr{text/html}, 'dashboard response keeps content type');
ok(!exists $first->{headers}{connection}, 'dashboard response does not force close');
like($first->{body}, qr{upsgi browser mix fixture}, 'dashboard body is returned over keepalive');

print {$sock} "GET /api/profile HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n";
my $second = read_http_response($sock);
like($second->{status_line}, qr{\AHTTP/1\.[01] 200\b}, 'api profile response returns 200');
like($second->{headers}{'content-length'}[0], qr{\A\d+\z}, 'api profile response auto-adds content length');
like($second->{headers}{'content-type'}[0], qr{application/json}, 'api profile response keeps content type');
like($second->{body}, qr{fixture-user}, 'api profile body is returned on the same connection');
is($second->{headers}{connection}[0], 'close', 'second response advertises Connection close');
my $eof = sysread_exact($sock, 1);
is($eof, '', 'socket closes after Connection close request');
close $sock;

done_testing();
