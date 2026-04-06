package UpSGITest;

use strict;
use warnings;

use Exporter 'import';
use File::Path qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);
use HTTP::Tiny;
use IO::Socket::INET;

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
    slurp
    run_ok
    reload_server
    pick_port
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
    if ($ENV{UPSGI_BIN}) {
        return $ENV{UPSGI_BIN};
    }
    return File::Spec->catfile(repo_root(), 'upsgi');
}

sub run_ok {
    my (@cmd) = @_;
    system(@cmd);
    return $? == 0;
}


sub pick_port {
    my ($offset) = @_;
    $offset ||= 0;
    if (defined $ENV{UPSGI_TEST_PORT}) {
        return $ENV{UPSGI_TEST_PORT} + $offset;
    }
    my $base = $ENV{UPSGI_TEST_PORT_BASE} || 19080;
    for my $port (($base + $offset) .. ($base + $offset + 2000)) {
        my $sock = IO::Socket::INET->new(
            LocalAddr => '127.0.0.1',
            LocalPort => $port,
            Proto => 'tcp',
            Listen => 1,
            ReuseAddr => 0,
        );
        if ($sock) {
            close $sock;
            return $port;
        }
    }
    die "unable to find an available test port\n";
}

sub render_profile {
    my (%args) = @_;
    my $helper = helper_path('profiles.sh');
    my @cmd = (
        $helper,
        'render',
        $args{profile},
        $args{output_ini},
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
    die "failed to start server\n" unless run_ok($helper, $args{binary}, $args{config_ini}, $args{artifact_dir});
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
