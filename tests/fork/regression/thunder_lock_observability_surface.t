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
my $utils = slurp_file(File::Spec->catfile($repo_root, 'core', 'utils.c'));
my $stats = slurp_file(File::Spec->catfile($repo_root, 'core', 'master_utils.c'));
my $metrics = slurp_file(File::Spec->catfile($repo_root, 'core', 'metrics.c'));
my $doc = slurp_file(File::Spec->catfile($repo_root, 'docs', 'performance', 'THUNDER_LOCK_REVIEW.md'));
my $lock = slurp_file(File::Spec->catfile($repo_root, 'core', 'lock.c'));

for my $field (qw(thunder_lock_acquires thunder_lock_contention_events thunder_lock_wait_us thunder_lock_hold_us thunder_lock_bypass_count)) {
    like($header, qr/\b$field\b/, "worker struct exposes $field");
    like($stats, qr/"$field"/, "stats surface includes $field");
}

unlike($header, qr/\bthunder_lock_eagain_accepts\b/, 'worker struct no longer exposes the TL2 EAGAIN counter');
unlike($header, qr/\baccept_lock\b/, 'socket struct no longer exposes the TL2 per-socket accept lock');

like($utils, qr/upsgi_accept_wait_lock_acquire/, 'accept path uses the wait-phase observability helper');
like($utils, qr/upsgi_accept_lock_release/, 'accept path uses the observability helper for lock release');
unlike($utils, qr/upsgi_accept_socket_lock_acquire/, 'accept path no longer uses the TL2 per-socket helper');
like($utils, qr/thunder_lock_bypass_count\+\+/, 'bypass counter increments when worker-local sockets skip the thunder lock');
like($utils, qr/thunder_lock_hold_us \+=/, 'hold time is accumulated on release');

for my $metric (qw(worker.%d.thunder_lock_acquires worker.%d.thunder_lock_contention_events worker.%d.thunder_lock_wait_us worker.%d.thunder_lock_hold_us worker.%d.thunder_lock_bypass_count)) {
    like($metrics, qr/\Q$metric\E/, "metrics surface includes $metric");
}
unlike($metrics, qr/thunder_lock_eagain_accepts/, 'metrics surface no longer includes the TL2 EAGAIN metric');

like($doc, qr/TL1 status: implemented and kept/, 'review doc records TL1 as retained');
like($doc, qr/TL2 status: measured and removed from the live tree/, 'review doc records TL2 as removed');
like($doc, qr/upsgi_mutex_fatal/, 'review doc notes the lock failure hard-stop path');
like($lock, qr/upsgi_mutex_fatal/, 'lock code includes the fatal mutex failure path');
unlike($doc, qr/thunder-lock-per-socket/, 'review doc no longer documents the removed TL2 option');

done_testing();
