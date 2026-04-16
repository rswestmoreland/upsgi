package UpSGITest;

use strict;
use warnings;

use Exporter 'import';
use File::Path qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);
use HTTP::Tiny;
use IO::Socket::INET;

my %ALLOCATED_PORTS;

our @EXPORT_OK = qw(
    fork_root
    repo_root
    helper_path
    fixture_app
    fixture_static_root
    build_artifact_dir
    default_binary
    render_profile
    start_server
    stop_server
    wait_http
    http_get
    http_request
    raw_upsgi_request
    slurp
    run_ok
    reload_server
    pick_port
    append_yaml_options
    fetch_stats_json
);

sub fork_root {
    my $path = File::Spec->rel2abs(__FILE__);
    $path =~ s{[\\/]+lib[\\/]+UpSGITest\.pm$}{};
    return $path;
}

sub repo_root {
    return File::Spec->catdir(fork_root(), '..', '..');
}

sub helper_path {
    return File::Spec->catfile(fork_root(), 'helpers', $_[0]);
}

sub fixture_app {
    return File::Spec->catfile(fork_root(), 'fixtures', 'apps', $_[0]);
}

sub fixture_static_root {
    return File::Spec->catdir(fork_root(), 'fixtures', 'static');
}

sub build_artifact_dir {
    my ($name) = @_;
    my $base = File::Spec->catdir(fork_root(), 'artifacts');
    make_path($base);
    my $dir = tempdir($name . '_XXXX', DIR => $base, CLEANUP => 0);
    make_path($dir);
    return $dir;
}

sub default_binary {
    if ($ENV{UPTEST_BIN}) {
        return $ENV{UPTEST_BIN};
    }
    if ($ENV{UPSGI_BIN}) {
        return $ENV{UPSGI_BIN};
    }
    return File::Spec->catfile(repo_root(), 'upsgi');
}

sub run_ok {
    my (@cmd) = @_;
    local @ENV{qw(UPSGI_BIN UPSGI_TEST_PORT UPSGI_TEST_PORT_BASE UPTEST_BIN UPTEST_PORT UPTEST_PORT_BASE)};
    system(@cmd);
    return $? == 0;
}

sub pick_port {
    my ($offset) = @_;
    $offset ||= 0;
    if (defined $ENV{UPTEST_PORT}) {
        return $ENV{UPTEST_PORT} + $offset;
    }
    if (defined $ENV{UPSGI_TEST_PORT}) {
        return $ENV{UPSGI_TEST_PORT} + $offset;
    }
    my $base = $ENV{UPTEST_PORT_BASE} || $ENV{UPSGI_TEST_PORT_BASE} || 19080;
    for my $port (($base + $offset) .. ($base + $offset + 2000)) {
        next if $ALLOCATED_PORTS{$port};
        my $sock = IO::Socket::INET->new(
            LocalAddr => '127.0.0.1',
            LocalPort => $port,
            Proto => 'tcp',
            Listen => 1,
            ReuseAddr => 0,
        );
        if ($sock) {
            close $sock;
            $ALLOCATED_PORTS{$port} = 1;
            return $port;
        }
    }
    die "unable to find an available test port\n";
}

sub append_yaml_options {
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

sub render_profile {
    my (%args) = @_;
    my $helper = helper_path('profiles.sh');
    my @cmd = (
        $helper,
        'render',
        $args{profile},
        $args{output_yaml},
        $args{app},
        $args{static_root},
        $args{log_file},
        $args{port},
    );
    die "failed to render profile\n" unless run_ok(@cmd);
}

sub start_server {
    my (%args) = @_;
    my $helper = helper_path('start_server.sh');
    die "failed to start server\n" unless run_ok($helper, $args{binary}, $args{config_yaml}, $args{artifact_dir});
}

sub stop_server {
    my ($artifact_dir) = @_;
    my $helper = helper_path('stop_server.sh');
    run_ok($helper, $artifact_dir);
}

sub wait_http {
    my (%args) = @_;
    my $helper = helper_path('wait_http.sh');
    return run_ok(
        $helper,
        $args{host},
        $args{port},
        ($args{path} || '/'),
        ($args{attempts} || 100),
        ($args{sleep_seconds} || 0.10),
    );
}

sub http_get {
    my (%args) = @_;
    local @ENV{qw(HTTP_PROXY http_proxy HTTPS_PROXY https_proxy ALL_PROXY all_proxy NO_PROXY no_proxy)};
    my $http = HTTP::Tiny->new(timeout => ($args{timeout} || 5), env_proxy => 0);
    my $url = sprintf('http://%s:%s%s', $args{host}, $args{port}, ($args{path} || '/'));
    my $headers = $args{headers} || {};
    my $resp = $http->get($url, { headers => $headers });
    return $resp;
}


sub fetch_stats_json {
    my (%args) = @_;
    my $host = $args{host} || '127.0.0.1';
    my $port = $args{port} || die "port is required for fetch_stats_json\n";
    my $attempts = $args{attempts} || 40;
    my $sleep_seconds = defined $args{sleep_seconds} ? $args{sleep_seconds} : 0.10;
    my $last_error = 'no stats response';

    for (1 .. $attempts) {
        my $sock = IO::Socket::INET->new(
            PeerAddr => $host,
            PeerPort => $port,
            Proto => 'tcp',
            Timeout => ($args{timeout} || 5),
        );
        if (!$sock) {
            $last_error = "unable to connect to stats socket on $host:$port: $!";
            select undef, undef, undef, $sleep_seconds;
            next;
        }

        my $json = '';
        while (1) {
            my $buf = '';
            my $read = sysread($sock, $buf, 4096);
            last if !defined($read) || $read == 0;
            $json .= $buf;
        }
        close $sock;

        if (length $json) {
            my $decoded = eval { require JSON::PP; JSON::PP::decode_json($json) };
            return $decoded if $decoded;
            $last_error = $@ || 'stats payload was not valid JSON';
        }
        else {
            $last_error = 'stats socket returned an empty payload';
        }

        select undef, undef, undef, $sleep_seconds;
    }

    die "$last_error\n";
}
sub http_request {
    my (%args) = @_;
    local @ENV{qw(HTTP_PROXY http_proxy HTTPS_PROXY https_proxy ALL_PROXY all_proxy NO_PROXY no_proxy)};
    my $http = HTTP::Tiny->new(timeout => ($args{timeout} || 5), env_proxy => 0);
    my $url = sprintf('http://%s:%s%s', $args{host}, $args{port}, ($args{path} || '/'));
    my $headers = $args{headers} || {};
    my $options = { headers => $headers };
    $options->{content} = $args{content} if exists $args{content};
    my $method = $args{method} || 'GET';
    return $http->request($method, $url, $options);
}

sub raw_upsgi_request {
    my (%args) = @_;
    my $vars = $args{vars} || {};
    my $sock = IO::Socket::INET->new(
        PeerAddr => $args{host},
        PeerPort => $args{port},
        Proto => 'tcp',
        Timeout => ($args{timeout} || 5),
    ) or die "unable to connect to upsgi socket on $args{host}:$args{port}: $!\n";

    my $body = '';
    for my $key (sort keys %{$vars}) {
        my $value = defined $vars->{$key} ? $vars->{$key} : '';
        $body .= pack('v', length($key)) . $key . pack('v', length($value)) . $value;
    }

    my $packet = pack('CvC', ($args{modifier1} || 0), length($body), ($args{modifier2} || 0)) . $body;
    print {$sock} $packet or die "unable to write upsgi request: $!\n";
    shutdown($sock, 1);

    my $raw = '';
    while (1) {
        my $chunk = '';
        my $read = sysread($sock, $chunk, 4096);
        last if !defined($read) || $read == 0;
        $raw .= $chunk;
    }
    close $sock;

    my ($head, $content) = split(/\r\n\r\n/, $raw, 2);
    my @lines = defined($head) ? split(/\r\n/, $head) : ();
    my $status_line = shift @lines // '';
    my ($http_version, $status, $reason) = $status_line =~ m/^(HTTP\/\d+\.\d+)\s+(\d+)\s*(.*)$/;

    my %headers;
    for my $line (@lines) {
        next unless $line =~ /:/;
        my ($name, $value) = split(/:\s*/, $line, 2);
        $value =~ s/\r\z// if defined $value;
        $headers{lc $name} = $value;
    }

    $reason =~ s/\r\z// if defined $reason;

    return {
        raw => $raw,
        http_version => $http_version,
        status => defined($status) ? int($status) : 0,
        reason => defined($reason) ? $reason : '',
        headers => \%headers,
        content => defined($content) ? $content : '',
        success => defined($status) && $status =~ /^2/ ? 1 : 0,
    };
}

sub reload_server {
    my ($artifact_dir) = @_;
    my $helper = helper_path('reload_server.sh');
    return run_ok($helper, $artifact_dir);
}

sub slurp {
    my ($path) = @_;
    open my $fh, '<', $path or die "unable to open $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    return $text;
}

1;
