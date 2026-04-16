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

my $logging = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'LOGGING.md'));
my $contract = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'LOSSLESS_LOGGER_QUEUE.md'));
my $summary = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'SI9_LOG_QUEUE_TUNING.md'));
my $small = slurp_file(File::Spec->catfile($repo_root, 'examples', 'upsgi', 'logging_small_host.yaml'));
my $chatty = slurp_file(File::Spec->catfile($repo_root, 'examples', 'upsgi', 'logging_chatty_sink.yaml'));

like($logging, qr/log-queue-records: 512/, 'logging guide documents the tuned default record cap');
like($logging, qr/log-queue-bytes: 512 KiB/, 'logging guide documents the tuned default byte cap');
like($logging, qr/logging_small_host\.yaml/, 'logging guide links the small-host profile');
like($logging, qr/logging_chatty_sink\.yaml/, 'logging guide links the chatty-sink profile');

like($contract, qr/512 records per queue/, 'queue contract documents the tuned default record cap');
like($contract, qr/512 KiB per queue/, 'queue contract documents the tuned default byte cap');

like($summary, qr/512 records per queue/, 'SI9 summary documents the tuned default record cap');
like($summary, qr/512 KiB per queue/, 'SI9 summary documents the tuned default byte cap');

like($small, qr/log-queue-records: 256/, 'small-host example lowers the record cap');
like($small, qr/log-queue-bytes: 262144/, 'small-host example lowers the byte cap');
like($chatty, qr/log-queue-records: 1024/, 'chatty-sink example raises the record cap');
like($chatty, qr/log-queue-bytes: 1048576/, 'chatty-sink example raises the byte cap');

done_testing();
