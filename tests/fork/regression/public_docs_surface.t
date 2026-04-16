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

my $readme = slurp_file(File::Spec->catfile($repo_root, 'README.md'));
my $index = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'INDEX.md'));
my $hardening = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'HARDENING.md'));
my $config = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'CONFIG_GUIDE.md'));
my $args = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'ARGUMENT_REFERENCE.md'));
my $body_example = slurp_file(File::Spec->catfile($repo_root, 'examples', 'upsgi', 'body_scheduler.yaml'));
my $template = slurp_file(File::Spec->catfile($repo_root, 'examples', 'upsgi', 'upsgi.example.yaml'));

unlike($readme, qr/NEXT_CHAT_HANDOFF\.md/, 'README does not expose handoff doc in curated entry list');
unlike($readme, qr/SAFE_FAMILY_REMOVAL_PLAN\.md/, 'README does not expose removal work plan in curated entry list');
unlike($index, qr/NEXT_CHAT_HANDOFF\.md/, 'INDEX does not expose handoff doc in curated entry list');
unlike($index, qr/SAFE_FAMILY_REMOVAL_PLAN\.md/, 'INDEX does not expose removal work plan in curated entry list');

like($readme, qr/routing = false/i, 'README explains default build keeps routing disabled');
like($hardening, qr/Reverse-proxy trust boundary/i, 'hardening guide includes reverse-proxy trust section');
like($hardening, qr/enable-proxy-protocol/i, 'hardening guide documents PROXY protocol caution');
like($config, qr/log-x-forwarded-for/i, 'config guide documents forwarded-for logging control');
like($args, qr/--enable-proxy-protocol/, 'argument reference documents enable-proxy-protocol');
like($args, qr/--log-x-forwarded-for/, 'argument reference documents log-x-forwarded-for');

unlike($body_example, qr/^\s*body-scheduler:\s*true\s*$/m, 'body-scheduler example relies on default-on posture');
like($template, qr/enable-proxy-protocol:/, 'example template shows proxy protocol toggle as commented guidance');
like($template, qr/log-x-forwarded-for:/, 'example template shows forwarded-for logging toggle as commented guidance');

done_testing();
