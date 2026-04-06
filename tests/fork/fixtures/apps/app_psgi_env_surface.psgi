use strict;
use warnings;

sub bool_line {
    my ($name, $value) = @_;
    return $name . '=' . ($value ? 1 : 0) . "\n";
}

sub {
    my $env = shift;

    my $body = '';
    $body .= "request_method=$env->{REQUEST_METHOD}\n";
    $body .= "path_info=$env->{PATH_INFO}\n";
    $body .= "query_string=$env->{QUERY_STRING}\n";
    $body .= "request_uri=$env->{REQUEST_URI}\n";
    $body .= "server_protocol=$env->{SERVER_PROTOCOL}\n";
    $body .= "url_scheme=$env->{'psgi.url_scheme'}\n";
    $body .= "version=" . join('.', @{ $env->{'psgi.version'} || [] }) . "\n";
    $body .= bool_line('has_errors', exists $env->{'psgi.errors'} && defined $env->{'psgi.errors'});
    $body .= bool_line('has_input', exists $env->{'psgi.input'} && defined $env->{'psgi.input'});
    $body .= bool_line('has_logger', exists $env->{'psgix.logger'} && defined $env->{'psgix.logger'});
    $body .= bool_line('has_cleanup_handlers', ref($env->{'psgix.cleanup.handlers'}) eq 'ARRAY');
    $body .= bool_line('psgi.multiprocess', $env->{'psgi.multiprocess'});
    $body .= bool_line('psgi.multithread', $env->{'psgi.multithread'});
    $body .= bool_line('psgi.nonblocking', $env->{'psgi.nonblocking'});
    $body .= bool_line('psgi.run_once', $env->{'psgi.run_once'});
    $body .= bool_line('psgi.streaming', $env->{'psgi.streaming'});
    $body .= bool_line('psgix.cleanup', $env->{'psgix.cleanup'});
    $body .= bool_line('psgix.harakiri', $env->{'psgix.harakiri'});
    $body .= bool_line('psgix.input.buffered', $env->{'psgix.input.buffered'});

    return [
        200,
        [ 'Content-Type' => 'text/plain' ],
        [ $body ],
    ];
};
