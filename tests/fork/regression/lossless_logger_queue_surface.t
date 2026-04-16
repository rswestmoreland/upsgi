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
my $opts = slurp_file(File::Spec->catfile($repo_root, 'core', 'upsgi.c'));
my $init = slurp_file(File::Spec->catfile($repo_root, 'core', 'init.c'));
my $stats = slurp_file(File::Spec->catfile($repo_root, 'core', 'master_utils.c'));
my $doc = slurp_file(File::Spec->catfile($repo_root, 'docs', 'upsgi', 'LOSSLESS_LOGGER_QUEUE.md'));

like($header, qr/struct upsgi_log_queue_item \{/, 'header defines queue item structure');
like($header, qr/struct upsgi_log_queue \{/, 'header defines queue structure');
like($header, qr/log_queue_enabled;/, 'header exposes log_queue_enabled tunable');
like($header, qr/log_queue_records;/, 'header exposes log_queue_records tunable');
like($header, qr/log_queue_bytes;/, 'header exposes log_queue_bytes tunable');
like($header, qr/struct upsgi_log_queue req_logger_queue;/, 'header exposes the request logger queue');

like($opts, qr/"log-queue-enabled"/, 'option table exposes log-queue-enabled');
like($opts, qr/"log-queue-records"/, 'option table exposes log-queue-records');
like($opts, qr/"log-queue-bytes"/, 'option table exposes log-queue-bytes');
like($opts, qr/"disable-log-queue"/, 'option table exposes disable-log-queue');

like($init, qr/upsgi\.log_queue_enabled = 1;/, 'queue starts enabled by default');
like($init, qr/upsgi\.log_queue_records = 512;/, 'default record cap is defined');
like($init, qr/upsgi\.log_queue_bytes = 512 \* 1024;/, 'default byte cap is defined');

like($stats, qr/"log_queue_depth"/, 'stats surface includes log_queue_depth');
like($stats, qr/"log_queue_bytes"/, 'stats surface includes log_queue_bytes');
like($stats, qr/"log_queue_depth_max"/, 'stats surface includes log_queue_depth_max');
like($stats, qr/"log_queue_bytes_max"/, 'stats surface includes log_queue_bytes_max');
like($stats, qr/"log_queue_backpressure_events"/, 'stats surface includes log_queue_backpressure_events');
like($stats, qr/"req_log_queue_depth"/, 'stats surface includes req_log_queue_depth');
like($stats, qr/"req_log_queue_backpressure_events"/, 'stats surface includes req_log_queue_backpressure_events');
like($stats, qr/"log_queue_batch_flush_events"/, 'stats surface includes log_queue_batch_flush_events');
like($stats, qr/"log_queue_batch_items"/, 'stats surface includes log_queue_batch_items');
like($stats, qr/"req_log_queue_batch_flush_events"/, 'stats surface includes req_log_queue_batch_flush_events');
like($stats, qr/"req_log_queue_batch_items"/, 'stats surface includes req_log_queue_batch_items');
like($stats, qr/"log_sink_recovery_attempts"/, 'stats surface includes log_sink_recovery_attempts');
like($stats, qr/"req_log_sink_recovery_successes"/, 'stats surface includes req_log_sink_recovery_successes');

like($doc, qr/log-queue-records/, 'contract documents log-queue-records');
like($doc, qr/log-queue-bytes/, 'contract documents log-queue-bytes');
like($doc, qr/log-queue-enabled/, 'contract documents log-queue-enabled');
like($doc, qr/disable-log-queue/, 'contract documents disable-log-queue');
like($doc, qr/512 records per queue/, 'contract documents the default record cap guidance');
like($doc, qr/512 KiB per queue/, 'contract documents the default byte cap guidance');

done_testing();
