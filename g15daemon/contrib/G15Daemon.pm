package G15Daemon;
use warnings;
use strict;

use Inline C =>
      DATA => 
      LIBS => '-lg15daemon_client';

our $VERSION = '0.1';
our $WIDTH = '160';
our $HEIGHT = '43';

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

sub newscreen {
  my $self = shift;
  my ($screentype) = @_;
  return g15newscreen($screentype);
}

sub closescreen {
  my $self = shift;
  my ($gsockfd) = @_;
  return g15closescreen($gsockfd);
}

sub send {
  my $self = shift;
  my ($sockfd, $buffer, $len) = @_;
  return g15send($sockfd, $buffer, $len);
}

sub recv{
  my $self = shift;
  my ($sockfd, $buffer, $len) = @_;
  return g15recv($sockfd, $buffer, $len);
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
  return g15_send(sockfd, buf, len);
}

char * g15recv(int sockfd, char *buf, unsigned int len) {
  g15_recv(sockfd, buf, len);
  return buf;
}

