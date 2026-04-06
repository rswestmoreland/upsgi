my $app = sub {
    my $env = shift;
    return [
        200,
        [ 'Content-Type' => 'text/plain' ],
        [ "upsgi example app\nmethod=$env->{REQUEST_METHOD}\npath=$env->{PATH_INFO}\n" ],
    ];
};

$app;
