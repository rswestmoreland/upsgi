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
my $readme = slurp_file(File::Spec->catfile($repo_root, 'README.md'));
my $defaults = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'RUNTIME_DEFAULTS.md'));
my $thunder = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'THUNDER_LOCK.md'));
my $body = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'BODY_SCHEDULER.md'));
my $naming = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'OPTION_NAMING.md'));

like($upsgi, qr/"disable-thunder-lock"/, 'CLI keeps disable-thunder-lock');
unlike($upsgi, qr/"no-thunder-lock"/, 'CLI no longer exposes no-thunder-lock');
like($upsgi, qr/"disable-body-scheduler"/, 'CLI keeps disable-body-scheduler');
unlike($upsgi, qr/"no-body-scheduler"/, 'CLI no longer exposes no-body-scheduler');

like($naming, qr/disable-\*/i, 'naming doc defines disable-* for default-on features');
like($naming, qr/enable-\*/i, 'naming doc defines enable-* for default-off features');

unlike($readme, qr/no-thunder-lock/, 'README no longer documents no-thunder-lock');
unlike($readme, qr/no-body-scheduler/, 'README no longer documents no-body-scheduler');
unlike($defaults, qr/no-thunder-lock/, 'runtime defaults doc no longer documents no-thunder-lock');
unlike($defaults, qr/no-body-scheduler/, 'runtime defaults doc no longer documents no-body-scheduler');
unlike($thunder, qr/no-thunder-lock/, 'thunder-lock doc no longer documents no-thunder-lock');
unlike($body, qr/no-body-scheduler/, 'body-scheduler doc no longer documents no-body-scheduler');

done_testing();
