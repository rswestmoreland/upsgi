my $app = sub {
    my $env = shift;
    if (($env->{PATH_INFO} || '') eq '/boom') {
        die "upsgi regression die marker\n";
    }
    return [
        200,
        [
            'Content-Type' => 'text/plain',
            'X-UpSGI-App' => 'die',
        ],
        ["upsgi die app ok\n"],
    ];
};
