use strict;
use warnings;
use Test::More;
use File::Spec::Functions qw(catfile);

my $repo_root = '.';
my $logging = slurp(catfile($repo_root, 'core', 'logging.c'));
my $doc = slurp(catfile($repo_root, 'docs', 'upsgi', 'LOSSLESS_LOGGER_QUEUE.md'));
my $readme = slurp(catfile($repo_root, 'tests', 'fork', 'README.md'));

like($logging, qr/static void upsgi_log_queue_flush_blocking\(struct upsgi_log_queue \*queue, int is_req_log\)/, 'logging core defines blocking queue flush');
like($logging, qr/while \(!upsgi_log_queue_has_room\(queue, len\)\)/, 'enqueue path waits for queue room');
like($logging, qr/upsgi_log_queue_flush_blocking\(queue, is_req_log\);\n\t\}/, 'enqueue path flushes queue under backpressure');
like($logging, qr/upsgi\.shared->log_queue_full_events\+\+;/, 'queue full counter is wired');
like($logging, qr/upsgi\.shared->req_log_queue_full_events\+\+;/, 'request queue full counter is wired');
like($logging, qr/upsgi\.shared->log_queue_backpressure_events\+\+;/, 'queue backpressure counter is wired');
like($logging, qr/upsgi\.shared->req_log_queue_backpressure_events\+\+;/, 'request queue backpressure counter is wired');
like($logging, qr/if \(upsgi\.log_queue_enabled && upsgi\.logger_queue\.head\) \{\n\t\tupsgi_log_queue_flush_blocking\(&upsgi\.logger_queue, 0\);/, 'generic drain path flushes its queue before returning');
like($logging, qr/if \(upsgi\.log_queue_enabled && upsgi\.req_logger_queue\.head\) \{\n\t\tupsgi_log_queue_flush_blocking\(&upsgi\.req_logger_queue, 1\);/, 'request drain path flushes its queue before returning');

like($doc, qr/The current source tree now includes the first active logger-side queue behavior:/, 'queue contract doc describes active behavior');
like($doc, qr/blocks the logger-side drain path instead of dropping log records/, 'queue contract doc describes lossless backpressure');
like($doc, qr/batch flushes for stream-style sinks/, 'queue contract doc describes batch flush behavior');
like($readme, qr/lossless_logger_queue_behavior\.t/, 'fork harness README lists the behavior guard');

done_testing();

sub slurp {
    my ($path) = @_;
    open my $fh, '<', $path or die "unable to open $path: $!";
    local $/;
    return <$fh>;
}
