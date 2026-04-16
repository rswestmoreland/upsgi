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
my $protocol = slurp_file(File::Spec->catfile($repo_root, 'core', 'protocol.c'));
my $psgi = slurp_file(File::Spec->catfile($repo_root, 'plugins', 'psgi', 'upsgi_plmodule.c'));
my $readme = slurp_file(File::Spec->catfile($repo_root, 'README.md'));
my $index = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'INDEX.md'));
my $boundary = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'SUPPORT_BOUNDARY.md'));

my @removed_parser_options = (
    'cache',
    'cache-blocksize',
    'cache-store',
    'cache-store-sync',
    'cache-no-expire',
    'cache-expire-freq',
    'cache-report-freed-items',
    'cache-udp-server',
    'cache-udp-node',
    'cache-sync',
    'cache-use-last-modified',
    'add-cache-item',
    'load-file-in-cache',
    'load-file-in-cache-gzip',
    'check-cache',
);

my @retained_parser_options = (
    'cache2',
    'static-cache-paths',
    'static-cache-paths-name',
    'ssl-sessions-use-cache',
    'ssl-session-use-cache',
);

for my $opt (@removed_parser_options) {
    unlike($upsgi, qr/"\Q$opt\E"/, "parser no longer exposes $opt");
}

for my $opt (@retained_parser_options) {
    like($upsgi, qr/"\Q$opt\E"/, "parser retains $opt");
}

unlike($protocol, qr/upsgi\.check_cache/, 'protocol no longer references check_cache');
unlike($psgi, qr/XS_cache_(?:set|get|exists|del|clear)/, 'PSGI cache XS helpers removed');
unlike($psgi, qr/psgi_xs\(cache_(?:get|exists|set|del|clear)\)/, 'PSGI cache XS registrations removed');

like($readme, qr/Generic cache management and cache-backed response lookup have been removed\./, 'README records reduced cache boundary');
like($index, qr/reduced local cache subset remains for static-path caching and SSL session cache support/i, 'INDEX records retained cache subset');
like($boundary, qr/Generic cache management and cache-backed response lookup/s, 'support boundary records reduced cache boundary');

done_testing();
