package G15Daemon;
use base 'Exporter';
use warnings;
use strict;

our @EXPORT = qw( $g15wbmp $g15pbuf $g15txt );

use Inline C =>
        DATA => 
        LIBS => '-lg15daemon_client';

our $VERSION = '0.2';
our $WIDTH = '160';
our $HEIGHT = '43';

our $g15pbuf = '0';
our $g15txt = '1';
our $g15wbmp = '2';

sub apiversion {
  my $self = shift;
  return g15version();
}

sub width {
  return $WIDTH;
}
sub height {
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
  return newSVpvf("%f\n", G15DAEMON_VERSION);
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

