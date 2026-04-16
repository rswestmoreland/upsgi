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

sub parser_option_regex {
    my ($opt) = @_;
    return qr/^\s*\{"\Q$opt\E",/m;
}

my $repo_root = File::Spec->catdir($FindBin::Bin, '..', '..', '..');

my $upsgi = slurp_file(File::Spec->catfile($repo_root, 'core', 'upsgi.c'));
my $config = slurp_file(File::Spec->catfile($repo_root, 'upsgiconfig.py'));
my $readme = slurp_file(File::Spec->catfile($repo_root, 'README.md'));
my $index = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'INDEX.md'));
my $boundary = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'SUPPORT_BOUNDARY.md'));
my $scope = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'REMOVAL_SCOPE_SUMMARY.md'));

my @removed_parser_options = (
    'legion', 'legion-mcast', 'legion-node', 'legion-freq', 'legion-tolerance',
    'legion-death-on-lord-error', 'legion-skew-tolerance', 'legion-lord',
    'legion-unlord', 'legion-setup', 'legion-death', 'legion-join',
    'legion-node-joined', 'legion-node-left', 'legion-quorum', 'legion-scroll',
    'legion-scroll-max-size', 'legion-scroll-list-max-size', 'legion-cron',
    'cron-legion', 'unique-legion-cron', 'unique-cron-legion',
    'legion-attach-daemon', 'legion-smart-attach-daemon', 'legion-smart-attach-daemon2',
    'queue', 'queue-blocksize', 'queue-store', 'queue-store-sync', 'sharedarea',
    'subscription-notify-socket', 'subscription-mountpoints', 'subscription-mountpoint',
    'subscription-vassal-required', 'subscriptions-credentials-check',
    'subscriptions-use-credentials', 'subscription-algo', 'subscription-dotsplit',
    'subscribe-to', 'st', 'subscribe', 'subscribe2', 'subscribe-freq',
    'subscription-tolerance', 'subscription-tolerance-inactive',
    'unsubscribe-on-graceful-reload', 'start-unsubscribed',
    'subscription-clear-on-shutdown', 'subscribe-with-modifier1',
    'cache', 'cache-blocksize', 'cache-store', 'cache-store-sync', 'cache-no-expire',
    'cache-expire-freq', 'cache-report-freed-items', 'cache-udp-server',
    'cache-udp-node', 'cache-sync', 'cache-use-last-modified', 'add-cache-item',
    'load-file-in-cache', 'load-file-in-cache-gzip', 'check-cache',
    'spooler', 'spooler-external', 'spooler-ordered', 'spooler-chdir',
    'spooler-processes', 'spooler-quiet', 'spooler-max-tasks',
    'spooler-signal-as-task', 'spooler-harakiri', 'spooler-frequency',
    'spooler-freq', 'spooler-cheap', 'mule', 'mules', 'farm', 'mule-msg-size',
    'mule-msg-recv-size', 'mule-harakiri', 'mule-reload-mercy', 'hook-as-mule',
    'touch-mules-reload', 'touch-spoolers-reload',
);

for my $opt (@removed_parser_options) {
    unlike($upsgi, parser_option_regex($opt), "parser no longer exposes $opt");
}

my @retained_parser_options = (
    'route',
    'route-run',
    'cache2',
    'static-cache-paths',
    'static-cache-paths-name',
    'ssl-sessions-use-cache',
    'ssl-session-use-cache',
);

for my $opt (@retained_parser_options) {
    like($upsgi, parser_option_regex($opt), "parser retains $opt");
}

for my $core_unit (qw(legion queue sharedarea subscription spooler mule)) {
    unlike($config, qr/'core\/$core_unit'/, "build manifest no longer includes core/$core_unit");
    ok(!-e File::Spec->catfile($repo_root, 'core', "$core_unit.c"), "core/$core_unit.c removed");
}

like($readme, qr/The conservative family-removal cleanup workstream is complete\./, 'README records completed workstream');
like($index, qr/The conservative family-removal cleanup workstream is complete\./, 'INDEX records completed workstream');
like($boundary, qr/Removed from the supported parser and runtime surface/s, 'support boundary records removed surface');
like($boundary, qr/Deferred from removal\s*- Routing family/s, 'support boundary records deferred routing');
like($scope, qr/Active removal candidates in this completed workstream\s*- none/s, 'scope summary records no active candidates');

done_testing();
