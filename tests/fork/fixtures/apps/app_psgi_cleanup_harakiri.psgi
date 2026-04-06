use strict;
use warnings;

{
    package upsgi::psgi::harakiri::watch;
    sub DESTROY { print STDERR "$$: harakiri destroy\n"; }
}

sub {
    my $env = shift;

    if ($env->{PATH_INFO} eq '/pid') {
        return [ 200, [ 'Content-Type' => 'text/plain' ], [ "pid=$$\n" ] ];
    }

    if ($env->{PATH_INFO} eq '/cleanup') {
        die "psgix.cleanup missing\n" unless $env->{'psgix.cleanup'};
        push @{ $env->{'psgix.cleanup.handlers'} }, sub { print STDERR "$$: cleanup one\n"; };
        push @{ $env->{'psgix.cleanup.handlers'} }, sub { print STDERR "$$: cleanup two\n"; };
        return [ 200, [ 'Content-Type' => 'text/plain' ], [ "pid=$$\ncleanup=queued\n" ] ];
    }

    if ($env->{PATH_INFO} eq '/harakiri') {
        die "psgix.harakiri missing\n" unless $env->{'psgix.harakiri'};
        $env->{'psgix.harakiri.commit'} = 1;
        $env->{'psgix.harakiri.watch'} = bless {}, 'upsgi::psgi::harakiri::watch';
        return [ 200, [ 'Content-Type' => 'text/plain' ], [ "pid=$$\ncommit=1\n" ] ];
    }

    return [ 404, [ 'Content-Type' => 'text/plain' ], [ "not found\n" ] ];
};
