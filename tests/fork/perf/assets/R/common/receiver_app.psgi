use strict;
use warnings;
use JSON::PP qw(decode_json encode_json);
use IO::Uncompress::Gunzip qw(gunzip $GunzipError);
use Time::HiRes qw(usleep);

sub _read_full_body {
    my ($env) = @_;
    my $in = $env->{'psgi.input'};
    return '' unless $in;
    my $buf = '';
    my $tmp = '';
    while (1) {
        my $read = $in->read($tmp, 65536);
        last unless defined $read && $read > 0;
        $buf .= $tmp;
        last if length($buf) >= (($env->{CONTENT_LENGTH} || 0) + 0) && ($env->{CONTENT_LENGTH} || '') ne '';
    }
    return $buf;
}

sub _inflate_if_needed {
    my ($env, $body) = @_;
    my $ct = lc($env->{CONTENT_TYPE} || '');
    return ($body, undef) unless $ct eq 'application/octet-stream';
    my $inflated = '';
    return (undef, "inflate failed: $GunzipError") unless gunzip(\$body => \$inflated);
    return ($inflated, undef);
}

sub _append_queue_line {
    my ($record) = @_;
    my $queue_file = $ENV{PERF_R_QUEUE_FILE} || '/tmp/upsgi_receiver.queue';
    if (my $delay_ms = ($ENV{PERF_R_APPEND_DELAY_MS} || 0)) {
        usleep($delay_ms * 1000);
    }
    open my $fh, '>>', $queue_file or die "open queue failed: $!";
    print {$fh} $record, "\n";
    close $fh;
}

our $app = sub {
    my ($env) = @_;
    my $path = $env->{PATH_INFO} || '/';
    my $method = $env->{REQUEST_METHOD} || 'GET';

    if ($path eq '/healthz') {
        return [200, ['Content-Type' => 'application/json'], [ encode_json({ ok => 1, queue_file => ($ENV{PERF_R_QUEUE_FILE} || '') }) ]];
    }

    return [404, ['Content-Type' => 'text/plain'], ["not found\n"]] unless $path eq '/push';
    return [405, ['Content-Type' => 'text/plain'], ["method not allowed\n"]] unless $method eq 'POST';

    my $body = _read_full_body($env);
    my ($decoded_body, $inflate_err) = _inflate_if_needed($env, $body);
    if ($inflate_err) {
        return [400, ['Content-Type' => 'application/json'], [ encode_json({ ok => 0, error => $inflate_err }) ]];
    }

    my $decoded;
    eval { $decoded = decode_json($decoded_body); 1 } or do {
        my $err = $@ || 'invalid json';
        return [400, ['Content-Type' => 'application/json'], [ encode_json({ ok => 0, error => "invalid json: $err" }) ]];
    };

    my $line = encode_json({
        received_bytes => length($body),
        decoded_bytes => length($decoded_body),
        content_type => ($env->{CONTENT_TYPE} || ''),
        source => ($decoded->{source} || 'unknown'),
        seq => ($decoded->{seq} || 0),
        payload_len => length($decoded->{payload} || ''),
    });

    eval { _append_queue_line($line); 1 } or do {
        my $err = $@ || 'append failed';
        return [500, ['Content-Type' => 'application/json'], [ encode_json({ ok => 0, error => $err }) ]];
    };

    return [202, ['Content-Type' => 'application/json'], [ encode_json({ ok => 1, accepted => 1 }) ]];
};

no warnings qw(void);
$app;
