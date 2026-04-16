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

my $header = slurp_file(File::Spec->catfile($repo_root, 'upsgi.h'));
my $lock = slurp_file(File::Spec->catfile($repo_root, 'core', 'lock.c'));
my $upsgi = slurp_file(File::Spec->catfile($repo_root, 'core', 'upsgi.c'));
my $doc = slurp_file(File::Spec->catfile($repo_root, 'docs', 'performance', 'THUNDER_LOCK_BACKENDS.md'));

for my $constant (qw(UPSGI_THUNDER_LOCK_BACKEND_NONE UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_ROBUST UPSGI_THUNDER_LOCK_BACKEND_PTHREAD_PLAIN UPSGI_THUNDER_LOCK_BACKEND_IPCSEM UPSGI_THUNDER_LOCK_BACKEND_FDLOCK)) {
    like($header, qr/\b$constant\b/, "header exposes $constant");
}

for my $field (qw(thunder_lock_backend_request thunder_lock_backend thunder_lock_backend_has_owner_dead_recovery thunder_lock_backend_reason)) {
    like($header, qr/\b$field\b/, "server struct exposes $field");
}

like($lock, qr/upsgi_set_thunder_lock_backend_state/, 'lock setup records thunder-lock backend state');
like($lock, qr/upsgi_thunder_lock_init/, 'lock setup routes thunder-lock creation through dedicated backend selection');
like($lock, qr/upsgi_lock_fd_init/, 'lock setup includes fd-lock thunder-lock support');
like($lock, qr/selected via --thunder-lock-backend=fdlock/, 'lock setup records explicit fd-lock selection');
like($lock, qr/plain pthread mode has no owner-death recovery; using fd-lock compatibility backend/, 'lock setup records automatic fd-lock fallback from degraded plain pthread mode');
unlike($lock, qr/pthread_create\(&tid, &attr, upsgi_robust_mutexes_watchdog_loop, NULL\)/, 'watchdog no longer spawns a recovery thread');

like($upsgi, qr/thunder lock backend:/, 'startup logging reports the thunder-lock backend');
like($upsgi, qr/thunder lock robust recovery:/, 'startup logging reports robust recovery state');
like($upsgi, qr/thunder lock watchdog diagnostics:/, 'startup logging reports watchdog diagnostics state');
like($upsgi, qr/thunder lock watchdog note: diagnostics only; no recovery thread is spawned/, 'startup logging notes diagnostic-only watchdog behavior');
like($upsgi, qr/thunder-lock-backend/, 'option table includes thunder-lock-backend');

like($doc, qr/fd-lock: implemented as the thunder-lock compatibility backend/, 'backend doc records fd-lock as implemented');
like($doc, qr/watchdog is diagnostic-only/, 'backend doc records diagnostic-only watchdog posture');

done_testing();
