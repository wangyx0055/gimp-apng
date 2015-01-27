#!/bin/sh

ACLOCAL=aclocal
AUTOHEADER=autoheader
AUTOMAKE=automake
AUTOCONF=autoconf
INTLTOOLIZE=intltoolize

set -x
if test ! -d config ; then
  mkdir config
fi
$ACLOCAL -I config && \
$AUTOHEADER -W all && \
$AUTOMAKE -W all --add-missing && \
$AUTOCONF -W all && \
$INTLTOOLIZE --automake
