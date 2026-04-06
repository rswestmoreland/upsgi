my $app = sub {
    my $env = shift;
    return [
        200,
        [
            'Content-Type' => 'text/plain',
            'X-UpSGI-App' => 'simple',
            'X-Request-Method' => ($env->{REQUEST_METHOD} || ''),
            'X-Path-Info' => ($env->{PATH_INFO} || ''),
        ],
        ["upsgi simple ok\npath=" . ($env->{PATH_INFO} || '') . "\n"],
    ];
};
