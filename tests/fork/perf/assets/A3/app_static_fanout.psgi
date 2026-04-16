use strict;
use warnings;
use JSON::PP qw(encode_json);

my @assets = (
    '/assets/css/reset.css',
    '/assets/css/layout.css',
    '/assets/css/theme.css',
    '/assets/js/runtime.js',
    '/assets/js/vendor.js',
    '/assets/js/app.js',
    '/assets/img/logo.svg',
    '/assets/img/hero.txt',
    '/assets/img/icon-16.txt',
    '/assets/img/icon-32.txt',
    '/assets/img/icon-64.txt',
    '/assets/img/bg-pattern.txt',
);

sub html_page {
    my $links = join "
", map {
        if (/\.css$/) {
            qq{<link rel="stylesheet" href="$_" />};
        }
        elsif (/\.js$/) {
            qq{<script src="$_"></script>};
        }
        else {
            qq{<img src="$_" alt="fixture" />};
        }
    } @assets;

    return <<"HTML";
<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<title>upsgi A3 static fanout fixture</title>
$links
</head>
<body>
<h1>upsgi static fanout fixture</h1>
<p>This page exists to generate an HTML hit followed by a fixed asset fanout.</p>
</body>
</html>
HTML
}

our $app = sub {
    my ($env) = @_;
    my $path = $env->{PATH_INFO} || '/';

    if ($path eq '/' || $path eq '/page') {
        return [
            200,
            [
                'Content-Type' => 'text/html; charset=utf-8',
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A3',
            ],
            [ html_page() ],
        ];
    }

    if ($path eq '/api/ping') {
        return [
            200,
            [
                'Content-Type' => 'application/json',
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A3',
            ],
            [ encode_json({ ok => JSON::PP::true, scenario => 'A3', assets => scalar(@assets) }) ],
        ];
    }

    return [404, ['Content-Type' => 'text/plain'], ["not found
"]];
};

no warnings qw(void);
$app;

