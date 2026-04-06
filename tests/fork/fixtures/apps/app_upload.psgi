my $app = sub {
    my $env = shift;
    my $input = $env->{'psgi.input'};
    my $body = '';
    if ($input) {
        while (1) {
            my $chunk = '';
            my $read = $input->read($chunk, 65536);
            last unless defined $read && $read > 0;
            $body .= $chunk;
        }
    }
    return [
        200,
        [
            'Content-Type' => 'text/plain',
            'X-UpSGI-App' => 'upload',
            'X-Body-Length' => length($body),
        ],
        [$body],
    ];
};
