return sub {
    my $env = shift;
    my $path = $env->{PATH_INFO} || '/';

    if ($path eq '/env') {
        my $body = join '',
            'input_buffered=', ($env->{'psgix.input.buffered'} ? '1' : '0'), "\n",
            'has_logger=', (defined $env->{'psgix.logger'} ? '1' : '0'), "\n",
            'has_errors=', (defined $env->{'psgi.errors'} ? '1' : '0'), "\n";
        return [
            200,
            [ 'Content-Type' => 'text/plain', 'X-UpSGI-App' => 'psgi-io-semantics' ],
            [ $body ],
        ];
    }

    if ($path eq '/error-print') {
        my $rv = $env->{'psgi.errors'}->print("psgi.errors smoke\n");
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ 'rv=', ($rv ? '1' : '0'), "\n" ],
        ];
    }

    if ($path eq '/logger-ok') {
        my $rv = $env->{'psgix.logger'}->({
            level => 'info',
            message => 'psgix logger ok',
        });
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ 'rv=', ($rv ? '1' : '0'), "\n" ],
        ];
    }

    if ($path eq '/logger-bad-level') {
        $env->{'psgix.logger'}->({
            level => 'trace',
            message => 'psgix logger bad level',
        });
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ "unexpected logger success\n" ],
        ];
    }

    if ($path eq '/input-seek') {
        my $in = $env->{'psgi.input'};
        my ($one, $two, $three, $four, $five, $six, $seven) = ('', '', '', '', '----', 'wxyz', 'AB');
        my $r1 = $in->read($one, 3);
        my $s1 = $in->seek(0, 0);
        my $r2 = $in->read($two, 3);
        my $s2 = $in->seek(-2, 1);
        my $r3 = $in->read($three, 3);
        my $s3 = $in->seek(-2, 2);
        my $r4 = $in->read($four, 2);
        my $s4 = $in->seek(0, 0);
        my $r5 = $in->read($five, 2, 1);
        my $s5 = $in->seek(0, 0);
        my $r6 = $in->read($six, 2, -2);
        my $s6 = $in->seek(0, 0);
        my $r7 = $in->read($seven, 2, -4);
        my $body = join '',
            'r1=', $r1, ' one=', $one, "\n",
            's1=', ($s1 ? '1' : '0'), "\n",
            'r2=', $r2, ' two=', $two, "\n",
            's2=', ($s2 ? '1' : '0'), "\n",
            'r3=', $r3, ' three=', $three, "\n",
            's3=', ($s3 ? '1' : '0'), "\n",
            'r4=', $r4, ' four=', $four, "\n",
            's4=', ($s4 ? '1' : '0'), "\n",
            'r5=', $r5, ' five=', $five, "\n",
            's5=', ($s5 ? '1' : '0'), "\n",
            'r6=', $r6, ' six=', $six, "\n",
            's6=', ($s6 ? '1' : '0'), "\n",
            'r7=', $r7, ' seven=', $seven, "\n";
        return [
            200,
            [ 'Content-Type' => 'text/plain' ],
            [ $body ],
        ];
    }

    return [
        404,
        [ 'Content-Type' => 'text/plain' ],
        [ "not found\n" ],
    ];
};
