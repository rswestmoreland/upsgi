use strict;
use warnings;

return sub {
    my $env = shift;
    my $path = $env->{PATH_INFO} || '/';

    if ($path eq '/has-info') {
        my $has = exists $env->{'psgix.informational'} ? 1 : 0;
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ "has_info=$has\n" ],
        ];
    }

    if ($path eq '/early') {
        my $cb = $env->{'psgix.informational'};
        if ($cb) {
            $cb->(
                103,
                [
                    'Link'    => '</style.css>; rel=preload; as=style',
                    'X-Early' => 'yes',
                ],
            );
        }

        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ "final body\n" ],
        ];
    }

    return [
        200,
        [ 'Content-Type' => 'text/plain' ],
        [ "ready\n" ],
    ];
};
