my $app = sub {
    my $env = shift;
    my $method = $env->{REQUEST_METHOD} || '';
    my $path = $env->{PATH_INFO} || '';

    if ($method eq 'POST' && $path eq '/upload') {
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
                'Content-Length' => length($body),
                'X-UpSGI-App' => 'http11-semantics',
                'X-Body-Length' => length($body),
            ],
            [$body],
        ];
    }

    my $body = "method=$method\npath=$path\n";
    return [
        200,
        [
            'Content-Type' => 'text/plain',
            'Content-Length' => length($body),
            'X-UpSGI-App' => 'http11-semantics',
            'X-Request-Method' => $method,
            'X-Path-Info' => $path,
        ],
        [$body],
    ];
};
