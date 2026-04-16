my $app = sub {
    return [
        200,
        [ 'Content-Type' => 'text/plain' ],
        [ "hello from upsgi\n" ],
    ];
};

$app;
