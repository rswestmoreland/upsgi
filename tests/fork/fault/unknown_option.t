use strict;
use warnings;

use File::Spec;
use FindBin;
use Test::More;

use lib File::Spec->catdir($FindBin::Bin, '..', 'lib');
use UpSGITest qw(pick_port build_artifact_dir default_binary fixture_app fixture_static_root slurp);

my $binary = default_binary();
my $artifact_dir = build_artifact_dir('fault_unknown_option');
my $config_ini = File::Spec->catfile($artifact_dir, 'unknown_option.ini');
my $server_log = File::Spec->catfile($artifact_dir, 'server.log');
my $port = pick_port(9);

open my $fh, '>', $config_ini or die "unable to write $config_ini: $!\n";
print {$fh} "[uwsgi]\n";
print {$fh} "master = true\n";
print {$fh} "workers = 1\n";
print {$fh} "need-app = true\n";
print {$fh} "strict = true\n";
print {$fh} "vacuum = true\n";
print {$fh} "die-on-term = true\n";
print {$fh} "http-socket = :$port\n";
print {$fh} "psgi = " . fixture_app('app_simple.psgi') . "\n";
print {$fh} "static-map = /assets=" . fixture_static_root() . "\n";
print {$fh} "logto = $server_log\n";
print {$fh} "definitely-not-a-real-option = true\n";
close $fh;

my $cmd = sprintf("%s --ini %s > %s 2>&1", quotemeta($binary), quotemeta($config_ini), quotemeta($server_log));
my $rc = system('sh', '-c', $cmd);
$rc = $rc >> 8;

ok($rc != 0, 'server exits nonzero for an unknown strict config option');
my $log_text = slurp($server_log);
like($log_text, qr/definitely-not-a-real-option/, 'failure log mentions the unknown option');
like($log_text, qr/unrecognized|unknown|invalid/i, 'failure log reports an unknown-option style error');

done_testing();
