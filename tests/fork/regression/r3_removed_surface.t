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
my $plmodule = slurp_file(File::Spec->catfile($repo_root, 'plugins', 'psgi', 'upsgi_plmodule.c'));
my $psgi_plugin = slurp_file(File::Spec->catfile($repo_root, 'plugins', 'psgi', 'psgi_plugin.c'));
my $psgi_loader = slurp_file(File::Spec->catfile($repo_root, 'plugins', 'psgi', 'psgi_loader.c'));
my $readme = slurp_file(File::Spec->catfile($repo_root, 'README.md'));
my $index = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'INDEX.md'));
my $boundary = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'SUPPORT_BOUNDARY.md'));
my $scope = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'REMOVAL_SCOPE_SUMMARY.md'));

my @removed_options = (
    'spooler',
    'spooler-external',
    'spooler-ordered',
    'spooler-chdir',
    'spooler-processes',
    'spooler-quiet',
    'spooler-max-tasks',
    'spooler-signal-as-task',
    'spooler-harakiri',
    'spooler-frequency',
    'spooler-freq',
    'spooler-cheap',
    'mule',
    'mules',
    'farm',
    'mule-msg-size',
    'mule-msg-recv-size',
    'mule-harakiri',
    'mule-reload-mercy',
    'hook-as-mule',
    'touch-mules-reload',
    'touch-spoolers-reload',
);

for my $opt (@removed_options) {
    unlike($upsgi, qr/"\Q$opt\E"/, "parser no longer exposes $opt");
}

unlike($config, qr/'core\/spooler'/, 'build manifest no longer includes core/spooler');
unlike($config, qr/'core\/mule'/, 'build manifest no longer includes core/mule');

ok(!-e File::Spec->catfile($repo_root, 'core', 'spooler.c'), 'core/spooler.c removed');
ok(!-e File::Spec->catfile($repo_root, 'core', 'mule.c'), 'core/mule.c removed');

unlike($plmodule, qr/XS_spooler\b/, 'PSGI spooler XS removed');
unlike($plmodule, qr/XS_spool\b/, 'PSGI spool XS removed');
unlike($plmodule, qr/upsgi_spool_request\b/, 'PSGI spool request bridge removed');
unlike($psgi_plugin, qr/upsgi_perl_spooler\b/, 'PSGI Perl spooler callback removed');
unlike($psgi_plugin, qr/\.spooler\s*=/, 'PSGI plugin no longer registers spooler callback');
unlike($psgi_plugin, qr/\.mule\s*=/, 'PSGI plugin no longer registers mule callback');
unlike($psgi_loader, qr/upsgi_perl_mule\b/, 'PSGI Perl mule helper removed');

like($readme, qr/Spooler \/ mule \/ farm has been removed from the supported parser and runtime surface\./, 'README records completed R3 removal');
like($index, qr/Spooler \/ mule \/ farm has been removed from the supported parser and runtime surface\./, 'INDEX records completed R3 removal');
like($boundary, qr/Spooler \/ mule \/ farm/s, 'support boundary records R3 removal');
like($scope, qr/Spooler \/ mule \/ farm family/s, 'removal scope summary records R3 family');

done_testing();
