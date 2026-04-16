use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

sub slurp_file {
    my ($path) = @_;
    open my $fh, '<', $path or die "open $path: $!";
    local $/;
    return <$fh>;
}

my $repo_root = File::Spec->catdir($FindBin::Bin, '..', '..', '..');
my $lock = slurp_file(File::Spec->catfile($repo_root, 'core', 'lock.c'));
my $upsgi = slurp_file(File::Spec->catfile($repo_root, 'core', 'upsgi.c'));

like($lock, qr/if \(!strcmp\(upsgi\.thunder_lock_backend_request, "fdlock"\)\)/, 'thunder-lock init recognizes explicit fdlock selection');
like($lock, qr/UPSGI_THUNDER_LOCK_BACKEND_FDLOCK/, 'fd-lock backend constant is used in runtime selection');
like($lock, qr/upsgi_lock_fd\(struct upsgi_lock_item \*uli\)/, 'fd-lock runtime lock helper exists');
like($lock, qr/upsgi_unlock_fd\(struct upsgi_lock_item \*uli\)/, 'fd-lock runtime unlock helper exists');
like($lock, qr/upsgi_tmpfd\(\)/, 'fd-lock runtime backend uses an anonymous temporary fd');
like($upsgi, qr/plain pthread fallback has no owner-death recovery; fd-lock compatibility backend is preferred/, 'startup warning points operators to fd-lock');

done_testing();
