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

my $upsgi = slurp_file(File::Spec->catfile($repo_root, 'core', 'upsgi.c'));
my $config = slurp_file(File::Spec->catfile($repo_root, 'upsgiconfig.py'));
my $readme = slurp_file(File::Spec->catfile($repo_root, 'README.md'));
my $index = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'INDEX.md'));
my $boundary = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'SUPPORT_BOUNDARY.md'));
my $scope = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'REMOVAL_SCOPE_SUMMARY.md'));

my @removed_options = (
    'queue',
    'queue-blocksize',
    'queue-store',
    'queue-store-sync',
    'sharedarea',
    'subscription-notify-socket',
    'subscription-mountpoints',
    'subscription-mountpoint',
    'subscription-vassal-required',
    'subscriptions-credentials-check',
    'subscriptions-use-credentials',
    'subscription-algo',
    'subscription-dotsplit',
    'subscribe-to',
    'st',
    'subscribe',
    'subscribe2',
    'subscribe-freq',
    'subscription-tolerance',
    'subscription-tolerance-inactive',
    'unsubscribe-on-graceful-reload',
    'start-unsubscribed',
    'subscription-clear-on-shutdown',
    'subscribe-with-modifier1',
);

for my $opt (@removed_options) {
    unlike($upsgi, qr/"\Q$opt\E"/, "parser no longer exposes $opt");
}

unlike($config, qr/'core\/queue'/, 'build manifest no longer includes core/queue');
unlike($config, qr/'core\/sharedarea'/, 'build manifest no longer includes core/sharedarea');
unlike($config, qr/'core\/subscription'/, 'build manifest no longer includes core/subscription');

ok(!-e File::Spec->catfile($repo_root, 'core', 'queue.c'), 'core/queue.c removed');
ok(!-e File::Spec->catfile($repo_root, 'core', 'sharedarea.c'), 'core/sharedarea.c removed');
ok(!-e File::Spec->catfile($repo_root, 'core', 'subscription.c'), 'core/subscription.c removed');

like($readme, qr/Queue \/ sharedarea \/ subscription has been removed/i, 'README records completed R2a removal');
like($index, qr/Queue \/ sharedarea \/ subscription has been removed/i, 'INDEX records completed R2a removal');
like($boundary, qr/Queue \/ sharedarea \/ subscription/s, 'support boundary records R2a removal');
like($scope, qr/Queue \/ sharedarea \/ subscription family/s, 'removal scope summary records R2a family');

done_testing();
