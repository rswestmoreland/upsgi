use strict;
use warnings;
use JSON::PP qw(encode_json);
use vars qw($app);

sub html_page {
    return <<'HTML';
<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<title>upsgi A1 browser mix fixture</title>
<link rel="stylesheet" href="/assets/css/app.css" />
<script src="/assets/js/app.js"></script>
</head>
<body>
<h1>upsgi browser mix fixture</h1>
<p>This fixture provides a small HTML page plus JSON and POST endpoints for keepalive traffic.</p>
<img src="/assets/img/logo.txt" alt="fixture" />
</body>
</html>
HTML
}

$app = sub {
    my ($env) = @_;
    my $path = $env->{PATH_INFO} || '/';
    my $method = $env->{REQUEST_METHOD} || 'GET';

    if ($method eq 'GET' && ($path eq '/' || $path eq '/dashboard')) {
        return [
            200,
            [
                'Content-Type' => 'text/html; charset=utf-8',
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A1',
            ],
            [ html_page() ],
        ];
    }

    if ($method eq 'GET' && $path eq '/api/profile') {
        return [
            200,
            [
                'Content-Type' => 'application/json',
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A1',
            ],
            [ encode_json({ ok => 1, scenario => 'A1', user => 'fixture-user' }) ],
        ];
    }

    if ($method eq 'GET' && $path eq '/api/health') {
        return [
            200,
            [
                'Content-Type' => 'application/json',
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A1',
            ],
            [ encode_json({ ok => 1, status => 'green', scenario => 'A1' }) ],
        ];
    }

    if ($method eq 'POST' && $path eq '/submit') {
        my $input = $env->{'psgi.input'};
        my $body = '';
        if ($input) {
            while (1) {
                my $chunk = '';
                my $read = $input->read($chunk, 8192);
                last unless defined $read && $read > 0;
                $body .= $chunk;
            }
        }
        return [
            200,
            [
                'Content-Type' => 'application/json',
                'Cache-Control' => 'no-store',
                'X-Perf-Scenario' => 'A1',
                'X-Body-Length' => length($body),
            ],
            [ encode_json({ ok => 1, scenario => 'A1', body_length => length($body) }) ],
        ];
    }

    return [404, ['Content-Type' => 'text/plain'], ["not found\n"]];
};
