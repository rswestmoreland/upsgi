my $app = sub {
    my $env = shift;
    my @lines = (
        'http_accept_language=' . ($env->{HTTP_ACCEPT_LANGUAGE} || ''),
        'http_x_forwarded_for=' . ($env->{HTTP_X_FORWARDED_FOR} || ''),
        'content_type=' . ($env->{CONTENT_TYPE} || ''),
        'http_x_test=' . ($env->{HTTP_X_TEST} || ''),
    );
    return [
        200,
        [
            'Content-Type' => 'text/plain',
            'X-UpSGI-App' => 'header-echo',
        ],
        [join("\n", @lines) . "\n"],
    ];
};
