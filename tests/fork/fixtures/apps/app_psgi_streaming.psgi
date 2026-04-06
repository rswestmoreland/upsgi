use strict;
use warnings;

use Time::HiRes qw(usleep);

sub {
    return sub {
        my $responder = shift;
        my $writer = $responder->([ 200, [ 'Content-Type', 'text/plain' ] ]);
        usleep(350_000);
        $writer->write("hello\n");
        usleep(350_000);
        $writer->write("world\n");
        $writer->close;
        return;
    };
};
