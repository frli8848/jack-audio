#!/bin/bash

VERSION=0.1.0

rm -fR aaudio-$VERSION
mkdir -p aaudio-$VERSION
mkdir -p aaudio-$VERSION/inst
mkdir -p aaudio-$VERSION/src
mkdir -p aaudio-$VERSION/doc
mkdir -p aaudio-$VERSION/

#
# The sources
#

cp Makefile aaudio-$VERSION/src/
cp aaudio.h aaudio-$VERSION/src/
cp aaudio.cc aaudio-$VERSION/src/
cp oct_aplay.cc aaudio-$VERSION/src/
cp oct_arecord.cc aaudio-$VERSION/src/
cp oct_aplayrec.cc aaudio-$VERSION/src/
cp oct_ainfo.cc aaudio-$VERSION/src/

#
# Mandatory pkg files
#

cp COPYING aaudio-$VERSION/
cp DESCRIPTION aaudio-$VERSION/
