#! /bin/sh
[ -d config ] || mkdir config 
set -x
aclocal -I config
libtoolize --force --copy
autoheader
automake --add-missing --copy
autoconf
