#!/bin/bash
#
# Scrip to build the aaudio octave package.
#
# Copyright (C) 2008, 2009, 2011 Fredrik Lingvall.
#
# 


VERSION=0.2.0

echo "Note add version number $VERSION to the DESCRIPTION file."

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
cp DESCRIPTION_ALSA aaudio-$VERSION/DESCRIPTION

tar cvzf aaudio-$VERSION.tar.gz aaudio-$VERSION/ 


#
# ALSA
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
cp DESCRIPTION_JACK jaudio-$VERSION/DESCRIPTION

tar cvzf jaudio-$VERSION.tar.gz jaudio-$VERSION/ 
