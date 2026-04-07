use strict;
use warnings;

use File::Spec;
use FindBin;
use IO::Socket::INET;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(build_artifact_dir default_binary fixture_app fixture_static_root pick_port render_profile start_server stop_server wait_http slurp);

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

sub read_until_close {
    my ($sock) = @_;
    my $raw = '';
    while (1) {
        my $chunk = '';
        my $rv = sysread($sock, $chunk, 4096);
        last if !defined($rv) || $rv == 0;
        $raw .= $chunk;
    }
    return $raw;
}

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('http11_semantics');
my $config_yaml = File::Spec->catfile($artifact_dir, 'http11.yaml');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(8);

render_profile(
    profile => 'baseline',
    output_yaml => $config_yaml,
    app => fixture_app('app_http11_semantics.psgi'),
    static_root => fixture_static_root(),
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

ok(wait_http(host => '127.0.0.1', port => $port, path => '/ready'), 'http11 profile becomes reachable');

my $sock = open_socket($port);
print {$sock} "GET /one HTTP/1.1\r\nHost: 127.0.0.1:$port\r\n\r\n";
my $first = read_http_response($sock);
like($first->{status_line}, qr{\AHTTP/1\.[01] 200\b}, 'first keepalive response returns 200');
is($first->{body}, "method=GET\npath=/one\n", 'first keepalive response body matches request path');
ok(!exists $first->{headers}{connection}, 'first keepalive response does not force Connection close');

print {$sock} "GET /two HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n";
my $second = read_http_response($sock);
like($second->{status_line}, qr{\AHTTP/1\.[01] 200\b}, 'second keepalive response returns 200');
is($second->{body}, "method=GET\npath=/two\n", 'second keepalive response body matches request path');
is($second->{headers}{connection}[0], 'close', 'second response advertises Connection close');
my $eof = sysread_exact($sock, 1);
is($eof, '', 'socket closes after Connection close request');
close $sock;

my $head_sock = open_socket($port);
print {$head_sock} "HEAD /head-check HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n\r\n";
my $head_raw = read_until_close($head_sock);
close $head_sock;
my ($head_headers, $head_body) = split /\r\n\r\n/, $head_raw, 2;
$head_body = '' unless defined $head_body;
like($head_headers, qr{\AHTTP/1\.[01] 200\b}, 'HEAD request returns 200');
like($head_headers, qr{\r\nContent-Length: \d+\r\n}i, 'HEAD response keeps Content-Length');
like($head_headers, qr{\r\nX-Request-Method: HEAD\r\n}i, 'HEAD response identifies the request method');
is($head_body, '', 'HEAD response sends no body bytes');

my $chunked_payload = "alpha=1\nbeta=two\n";
my $chunked_sock = open_socket($port);
print {$chunked_sock} "POST /upload HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nTransfer-Encoding: chunked\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n";
print {$chunked_sock} sprintf("%X\r\n%s\r\n0\r\n\r\n", length($chunked_payload), $chunked_payload);
my $chunked_raw = read_until_close($chunked_sock);
close $chunked_sock;
my ($chunked_headers, $chunked_body) = split /\r\n\r\n/, $chunked_raw, 2;
$chunked_body = '' unless defined $chunked_body;
like($chunked_headers, qr{\AHTTP/1\.[01] 200\b}, 'chunked upload returns 200');
like($chunked_headers, qr/\r\nX-Body-Length: 17\r\n/i, 'chunked upload reports the decoded body length');
is($chunked_body, $chunked_payload, 'chunked upload body is delivered through psgi.input');

done_testing();
