#!/bin/bash
#
# Script to build the aaudio and jaudio Octave packages.
#
# Copyright (C) 2008, 2009, 2011, 2012, 2013 Fredrik Lingvall.
#
#

PKG_VERSION=0.2.3
VERSION=`svnversion . | sed 's/:/-/'`
DATE=`date '+%Y-%m-%d'`

#
# ALSA
#

rm -fR aaudio-$VERSION
mkdir -p aaudio-$VERSION
mkdir -p aaudio-$VERSION/inst
mkdir -p aaudio-$VERSION/src
mkdir -p aaudio-$VERSION/doc
mkdir -p aaudio-$VERSION/

# The ALSA sources

cp Makefile_alsa aaudio-$VERSION/src/Makefile
cp aaudio.h aaudio-$VERSION/src/
cp aaudio.cc aaudio-$VERSION/src/
cp oct_aplay.cc aaudio-$VERSION/src/
cp oct_arecord.cc aaudio-$VERSION/src/
cp oct_atrecord.cc aaudio-$VERSION/src/
cp oct_aplayrec.cc aaudio-$VERSION/src/
cp oct_ainfo.cc aaudio-$VERSION/src/

# Mandatory pkg files

cp COPYING aaudio-$VERSION/
rm -f DESCRIPTION
cp DESCRIPTION_ALSA DESCRIPTION
sed -i -e "s/Version: */Version: $PKG_VERSION/" DESCRIPTION
sed -i -e "s/Date: */Date: $DATE/" DESCRIPTION
sed -i -e "s/Title: Aaudio/Title: Aaudio r$VERSION/" DESCRIPTION
cp DESCRIPTION aaudio-$VERSION/DESCRIPTION

tar cvzf aaudio-$VERSION.tar.gz aaudio-$VERSION/


#
# JACK
#

rm -fR jaudio-$VERSION
mkdir -p jaudio-$VERSION
mkdir -p jaudio-$VERSION/inst
mkdir -p jaudio-$VERSION/src
mkdir -p jaudio-$VERSION/doc
mkdir -p jaudio-$VERSION/

# The JACK sources

cp Makefile_jack jaudio-$VERSION/src/Makefile
cp jaudio.h jaudio-$VERSION/src/
cp jaudio.cc jaudio-$VERSION/src/
cp oct_jplay.cc jaudio-$VERSION/src/
cp oct_jrecord.cc jaudio-$VERSION/src/
cp oct_jtrecord.cc jaudio-$VERSION/src/
cp oct_jplayrec.cc jaudio-$VERSION/src/
cp oct_jinfo.cc jaudio-$VERSION/src/

# Mandatory pkg files

cp COPYING jaudio-$VERSION/
rm -f DESCRIPTION
cp DESCRIPTION_JACK DESCRIPTION
sed -i -e "s/Version: */Version: $PKG_VERSION/" DESCRIPTION
sed -i -e "s/Date: */Date: $DATE/" DESCRIPTION
sed -i -e "s/Title: Jaudio/Title: Jaudio r$VERSION/" DESCRIPTION
cp DESCRIPTION jaudio-$VERSION/DESCRIPTION

tar cvzf jaudio-$VERSION.tar.gz jaudio-$VERSION/
