package XML::Fast;

use 5.008008;
use strict;
use warnings;no warnings 'uninitialized';
use Encode;

use base 'Exporter';
our @EXPORT_OK = our @EXPORT = qw( xml2hash hash2xml );

our $VERSION = '0.14';

use XSLoader;
XSLoader::load('XML::Fast', $VERSION);

sub xml2hash($;%) {
	my $xml = shift;
	my %args = (
		order  => 0,        # not impl
		attr   => '-',      # ok
		text   => '#text',  # ok
		join   => '',       # ok
		trim   => 1,        # ok
		cdata  => undef,    # ok + fallback -> text
		comm   => undef,    # ok
		@_
	);
	_xml2hash($xml,\%args);
}

sub hash2xml($;%) {
	my $xml = shift;
	my %args = (
		order  => 0,        # not impl
		attr   => '-',      # ok
		text   => '#text',  # ok
		join   => '',       # ok
		trim   => 1,        # ok
		cdata  => undef,    # ok + fallback -> text
		comm   => undef,    # ok
		@_
	);
	_hash2xml($xml,\%args);
}

1;
__END__
=head1 NAME

XML::Fast - Simple and very fast XML - hash conversion

=head1 SYNOPSIS

  use XML::Fast;
  
  my $hash = xml2hash $xml;
  my $hash2 = xml2hash $xml, attr => '.', text => '~';

=head1 DESCRIPTION

This module implements simple, state machine based, XML parser written in C.

It could parse and recover some kind of broken XML's. If you need XML validator, use L<XML::LibXML>

=head1 RATIONALE

Another similar module is L<XML::Bare>. I've used it for some time, but it have some failures:

=over 4

=item * If your XML have node with name 'value', you'll got a segfault

=item * If your XML have node with TextNode, then CDATANode, then again TextNode, you'll got broken value

=item * It doesn't support charsets

=item * It doesn't support any kind of entities.

=back

So, after count of tries to fix L<XML::Bare> I've decided to write parser from scratch.

It is about 40% faster than L<XML::Bare> and about 120% faster, than L<XML::LibXML>

I got this results using the following test on 35kb xml doc:

    cmpthese timethese -10, {
        libxml  => sub { XML::LibXML->new->parse_string($doc) },
        xmlfast => sub { XML::Fast::xml2hash($doc) },
        xmlbare => sub { XML::Bare->new(text => $doc)->parse },
    };

              Rate  libxml xmlbare xmlfast
    libxml  1107/s      --    -38%    -56%
    xmlbare 1782/s     61%      --    -28%
    xmlfast 2490/s    125%     40%      --

Of course, the results could be defferent for different xml files.
With non-utf encodings and with many entities it could be slower.
This test was taken for a sample RSS feed in utf-8 mode with a small count of xml entities.

Here is some features and principles:

=over 4

=item * It uses minimal count of memory allocations.

=item * All XML is parsed in 1 scan.

=item * All values are copied from source XML only once (to destination keys/values)

=item * If some types of nodes (for ex comments) are ignored, there are no memory allocations/copy for them.

=back

=head1 EXPORT

=head2 xml2hash $xml, [ %options ]

=head2 hash2xml $hash, [ %options ]

=head1 OPTIONS

=over 4

=item order [ = 0 ]

B<Not implemented yet>. B<Strictly> keep the output order. When enabled, structures become more complex, but xml could be completely reverted. 

=item attr [ = '-' ]

Attribute prefix

    <node attr="test" />  =>  { node => { -attr => "test" } }

=item text [ = '#text' ]

Key name for storing text

When undef, text nodes will be ignored

    <node>text<sub /></node>  =>  { node => { sub => '', '#text' => "test" } }

=item join [ = '' ]

Join separator for text nodes, splitted by subnodes

Ignored when C<order> in effect

    # default:
    xml2hash( '<item>Test1<sub />Test2</item>' )
    : { item => { sub => '', '~' => 'Test1Test2' } };
    
    xml2hash( '<item>Test1<sub />Test2</item>', join => '+' )
    : { item => { sub => '', '~' => 'Test1+Test2' } };

=item trim [ = 1 ]

Trim leading and trailing whitespace from text nodes

=item cdata [ = undef ]

When defined, CDATA sections will be stored under this key

    # cdata = undef
    <node><![CDATA[ test ]]></node>  =>  { node => 'test' }

    # cdata = '#'
    <node><![CDATA[ test ]]></node>  =>  { node => { '#' => 'test' } }

=item comm [ = undef ]

When defined, comments sections will be stored under this key

When undef, comments will be ignored

    # comm = undef
    <node><!-- comm --><sub/></node>  =>  { node => { sub => '' } }

    # comm = '/'
    <node><!-- comm --><sub/></node>  =>  { node => { sub => '', '/' => 'comm' } }

=item array => 1

Force all nodes to be kept as arrays.

    # no array
    <node><sub/></node>  =>  { node => { sub => '' } }

    # array = 1
    <node><sub/></node>  =>  { node => [ { sub => [ '' ] } ] }

=item array => [ 'node', 'names']

Force nodes with names to be stored as arrays

    # no array
    <node><sub/></node>  =>  { node => { sub => '' } }

    # array => ['sub']
    <node><sub/></node>  =>  { node => { sub => [ '' ] } }

=back

=head1 SEE ALSO

=over 4

=item * L<XML::Bare>

Another fast parser, but have problems

=item * L<XML::LibXML>

The most powerful XML parser for perl. If you don't need to parse gigabytes of XML ;)

=item * L<XML::Hash::LX>

XML parser, that uses L<XML::LibXML> for parsing and then constructs hash structure, identical to one, generated by this module. (At least, it should ;)). But of course it is much more slower, than L<XML::Fast>

=back

=head1 TODO

=over 4

=item * Ordered mode (as implemented in L<XML::Hash::LX>)

=item * Create hash2xml, identical to one in L<XML::Hash::LX>

=item * Partial content event-based parsing (I need this for reading XML streams)

=back

Patches, propositions and bug reports are welcome ;)

=head1 AUTHOR

Mons Anderson, E<lt>mons@cpan.orgE<gt>

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2010 Mons Anderson

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself.

=cut
