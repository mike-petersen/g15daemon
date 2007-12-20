#    This file is part of g15daemon.

#    g15daemon is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    g15daemon is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with g15daemon; if not, write to the Free Software
#    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#    
#    (c) 2006-2007 Mike Lampard, Philip Lawatsch, and others
#    
#    $Revision: 293 $ -  $Date: 2007-09-08 23:37:12 +0930 (Sat, 08 Sep 2007) $ $Author: mlampard $
#    
#    This daemon listens on localhost port 15550 for client connections,
#    and arbitrates LCD display.  Allows for multiple simultaneous clients.
#    Client screens can be cycled through by pressing the 'L1' key.


package G15Daemon;
use base 'Exporter';
use warnings;
use strict;

use 5.008007;
our $VERSION = '0.2';

require Exporter;

our @EXPORT = qw( $g15wbmp $g15pbuf $g15txt );

use Inline C =>
        DATA => 
        LIBS => '-lg15daemon_client';

our $WIDTH = '160';
our $HEIGHT = '43';

*g15pbuf = \0;
*g15txt = \1;
*g15wbmp = \2;

sub apiversion {
  my $self = shift;
  return g15version();
}

sub width {
  my $self = shift;
  return $WIDTH;
}
sub height {
  my $self = shift;
  return $HEIGHT;
}

sub new
{
  my ($class, $screentype) = @_;
  my $sock = g15newscreen ($screentype);
  my $self = {socket => $sock};
  bless $self, $_[0];
}

sub DESTROY {
  my $self = shift;
  return g15closescreen($self->{socket});
}

sub closescreen {
  my $self = shift;
  return g15closescreen($self->{socket});
}

sub send {
  my $self = shift;
  my ($buffer) = @_;
  my $len = length($buffer);
  return g15send($self->{socket}, $buffer, $len);
}

sub recv{
  my $self = shift;
  my ($buffer, $len) = @_;
  return g15recv($self->{socket}, $buffer, $len);
}

1;

__DATA__
__C__

#include <g15daemon_client.h>

SV* g15version() {
  return newSVpvf("%s\n", G15DAEMON_VERSION);
}

int g15newscreen (int screentype) {
  return new_g15_screen(screentype);
}

int g15closescreen(int sockfd) {
  return g15_close_screen(sockfd);
}

int g15send(int sockfd, char *buf, unsigned int len) {
  int retval = g15_send(sockfd, buf, len);
  memset(buf,0,len);
  return retval;
}

char * g15recv(int sockfd, char *buf, unsigned int len) {
  g15_recv(sockfd, buf, len);
  return buf;
}

__END__
