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

my $init = slurp_file(File::Spec->catfile($repo_root, 'core', 'init.c'));
my $upsgi = slurp_file(File::Spec->catfile($repo_root, 'core', 'upsgi.c'));
my $readme = slurp_file(File::Spec->catfile($repo_root, 'README.md'));
my $defaults = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'RUNTIME_DEFAULTS.md'));
my $thunder = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'THUNDER_LOCK.md'));
my $body = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'BODY_SCHEDULER.md'));

like($init, qr/upsgi\.use_thunder_lock = 1;/, 'thunder lock defaults on in init');
like($init, qr/upsgi\.body_scheduler = 1;/, 'body scheduler defaults on in init');

like($upsgi, qr/"disable-thunder-lock"/, 'CLI exposes disable-thunder-lock');
like($upsgi, qr/"disable-body-scheduler"/, 'CLI exposes disable-body-scheduler');

like($readme, qr/thunder lock enabled by default/i, 'README documents thunder lock default-on posture');
like($readme, qr/body scheduler enabled by default/i, 'README documents body scheduler default-on posture');

like($defaults, qr/thunder lock/i, 'runtime defaults doc covers thunder lock');
like($defaults, qr/body scheduler/i, 'runtime defaults doc covers body scheduler');
like($thunder, qr/thundering herd/i, 'thunder-lock doc explains the thundering herd problem');
like($body, qr/enabled by default/i, 'body-scheduler doc records default-on posture');

done_testing();
