my $app = sub {
    my $env = shift;
    select undef, undef, undef, 2.0;
    return [
        200,
        [ 'Content-Type' => 'text/plain', 'X-UpSGI-App' => 'slow' ],
        ["upsgi slow ok\n"],
    ];
};
