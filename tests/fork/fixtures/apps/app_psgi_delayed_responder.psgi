use strict;
use warnings;

use Time::HiRes qw(usleep);

sub {
    return sub {
        my $responder = shift;
        usleep(250_000);
        $responder->([
            200,
            [
                'Content-Type' => 'text/plain',
                'Content-Length' => '13',
            ],
            ["delayed-body\n"],
        ]);
        return;
    };
};
