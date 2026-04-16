use strict;
use warnings;

{
    package ValidationStream;
    use strict;
    use warnings;

    sub new {
        my ($class, %args) = @_;
        my $self = {
            chunks     => $args{chunks} // 32,
            chunk_size => $args{chunk_size} // 65536,
            label      => $args{label} // 'alpha',
            sent       => 0,
        };
        return bless $self, $class;
    }

    sub getline {
        my ($self) = @_;
        return undef if $self->{sent} >= $self->{chunks};

        my $idx = $self->{sent};
        my $prefix = sprintf("chunk=%06d;label=%s;", $idx, $self->{label});
        my $body = $prefix;

        while (length($body) < $self->{chunk_size}) {
            $body .= sprintf("[%06d:%s]", $idx, $self->{label});
        }

        substr($body, $self->{chunk_size}) = '' if length($body) > $self->{chunk_size};
        $self->{sent}++;
        return $body;
    }

    sub close {
        return 1;
    }
}

sub parse_query {
    my ($qs) = @_;
    my %out;
    for my $pair (split /&/, ($qs // '')) {
        next unless length $pair;
        my ($k, $v) = split /=/, $pair, 2;
        $k //= '';
        $v //= '';
        $k =~ tr/+/ /;
        $v =~ tr/+/ /;
        $k =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
        $v =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
        $out{$k} = $v;
    }
    return \%out;
}

our $app = sub {
    my ($env) = @_;
    my $path = $env->{PATH_INFO} || '/';
    my $qs = parse_query($env->{QUERY_STRING});

    if ($path eq '/small') {
        return [
            200,
            [
                'Content-Type'   => 'text/plain',
                'Cache-Control'  => 'no-store',
                'X-Validation'   => 'small',
            ],
            ["ok\n"],
        ];
    }

    if ($path eq '/stream') {
        my $chunks = $qs->{chunks};
        my $chunk_size = $qs->{chunk_size};
        my $label = $qs->{label};

        $chunks = 32 unless defined $chunks && $chunks =~ /^\d+$/;
        $chunk_size = 65536 unless defined $chunk_size && $chunk_size =~ /^\d+$/;
        $label = 'alpha' unless defined $label && length $label;

        my $stream = ValidationStream->new(
            chunks     => $chunks,
            chunk_size => $chunk_size,
            label      => $label,
        );

        return [
            200,
            [
                'Content-Type'    => 'application/octet-stream',
                'Cache-Control'   => 'no-store',
                'X-Validation'    => 'stream',
                'X-Chunks'        => $chunks,
                'X-Chunk-Size'    => $chunk_size,
                'X-Stream-Label'  => $label,
            ],
            $stream,
        ];
    }

    return [
        404,
        ['Content-Type' => 'text/plain', 'Cache-Control' => 'no-store'],
        ["not found\n"],
    ];
};

no warnings qw(void);
$app;
