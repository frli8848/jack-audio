#!/usr/local/bin/make

# Copyright (C) 2008, 2009, 2011 Fredrik Lingvall
#
# This program is free software; you can redistribute it and/or modify 
# it under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
#
# The program is distributed in the hope that it will be useful, but 
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this software; see the file COPYING. If not, write to the 
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

# $Revision$ $Date$ $LastChangedBy

CXX = mkoctfile
DLDCC = mkoctfile

ALIBDIRS = -lasound
JLIBDIRS = -ljack -lpthread -lrt

all:

all: \
	ainfo.oct \
	aplay.oct \
	arecord.oct \
	atrecord.oct \
	actrecord.oct \
	aplayrec.oct


jack: \
	jinfo.oct \
	jplay.oct \
	jrecord.oct \
	jtrecord.oct \
	jplayrec.oct 

.cc.o:	
	$(CXX) -c $<
#
# ALSA
#

ainfo.oct : oct_ainfo.o aaudio.o
	$(DLDCC) $(ALIBDIRS) $^ -o $@

aplay.oct : oct_aplay.o aaudio.o
	$(DLDCC) $(ALIBDIRS) $^ -o $@ 

arecord.oct : oct_arecord.o aaudio.o
	$(DLDCC) $(ALIBDIRS) $^ -o $@ 

atrecord.oct : oct_atrecord.o aaudio.o
	$(DLDCC) $(ALIBDIRS) $^ -o $@ 

actrecord.oct : oct_actrecord.o aaudio.o
	$(DLDCC) $(ALIBDIRS) $^ -o $@ 

aplayrec.oct : oct_aplayrec.o aaudio.o
	$(DLDCC) -lpthread $^ -o $@ 

#
# JACK
#

jinfo.oct : oct_jinfo.o 
	$(DLDCC) $(JLIBDIRS) $^ -o $@

jplay.oct : oct_jplay.o jaudio.o
	$(DLDCC) $(JLIBDIRS) $^ -o $@

jrecord.oct : oct_jrecord.o jaudio.o
	$(DLDCC) $(JLIBDIRS) $^ -o $@

jtrecord.oct : oct_jtrecord.o jaudio.o
	$(DLDCC) $(JLIBDIRS) $^ -o $@

jplayrec.oct : oct_jplayrec.o jaudio.o
	$(DLDCC) $(JLIBDIRS) $^ -o $@

clean:
	rm -f *.o *~ 

distclean:
	rm -f *.o *~ *.oct
