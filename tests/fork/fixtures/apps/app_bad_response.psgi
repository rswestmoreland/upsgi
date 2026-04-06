package BadPath;
sub new { bless {}, shift }
sub path { return undef }

package main;

return sub {
    my $env = shift;
    my $path = $env->{PATH_INFO} || '/';

    if ($path eq '/ok') {
        return [
            200,
            [ 'Content-Type' => 'text/plain', 'X-UpSGI-App' => 'bad-response' ],
            [ "ok\n" ],
        ];
    }

    if ($path eq '/dup-set-cookie') {
        return [
            200,
            [
                'Content-Type' => 'text/plain',
                'Set-Cookie' => 'a=1',
                'Set-Cookie' => 'b=2',
            ],
            [ "cookies\n" ],
        ];
    }

    if ($path eq '/bad-status') {
        return [
            99,
            [ 'Content-Type' => 'text/plain' ],
            [ "bad status\n" ],
        ];
    }

    if ($path eq '/bad-arity') {
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
        ];
    }

    if ($path eq '/bad-headers') {
        return [
            200,
            [ 'Content-Type' ],
            [ "bad headers\n" ],
        ];
    }

    if ($path eq '/bad-header-name') {
        return [
            200,
            [ 'Bad Header' => 'oops', 'Content-Type' => 'text/plain' ],
            [ "bad header name\n" ],
        ];
    }

    if ($path eq '/bad-header-value') {
        return [
            200,
            [ 'X-Bad' => "line1\nline2", 'Content-Type' => 'text/plain' ],
            [ "bad header value\n" ],
        ];
    }

    if ($path eq '/missing-content-type') {
        return [
            200,
            [ 'X-No-Type' => '1' ],
            [ "missing type\n" ],
        ];
    }

    if ($path eq '/forbidden-content-type') {
        return [
            204,
            [ 'Content-Type' => 'text/plain' ],
            [ ],
        ];
    }

    if ($path eq '/forbidden-content-length') {
        return [
            204,
            [ 'Content-Length' => '0' ],
            [ ],
        ];
    }

    if ($path eq '/bad-body-hole') {
        my $body = [];
        $body->[1] = "late\n";
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            $body,
        ];
    }

    if ($path eq '/bad-body-wide') {
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ "wide smile \x{263a}\n" ],
        ];
    }

    if ($path eq '/bad-path') {
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            BadPath->new,
        ];
    }

    if ($path eq '/bad-logger') {
        $env->{'psgix.logger'}->('not-a-hashref');
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ "unexpected logger success\n" ],
        ];
    }

    if ($path eq '/bad-stream') {
        return sub {
            return uwsgi::stream('not-an-arrayref');
        };
    }

    return [
        404,
        [ 'Content-Type' => 'text/plain' ],
        [ "not found\n" ],
    ];
};
