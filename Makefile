#!/usr/local/bin/make

# Copyright (C) 2008 Fredrik Lingvall
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


CXX = mkoctfile
DLDCC = mkoctfile

LIBDIRS	= -lasound

all:

all: \
	aplay.oct \
	arecord.oct \
	aplayrec.oct \
	ainfo.oct

.cc.o:
	$(CXX) -c $<


aplay.oct : oct_aplay.o aaudio.o
	$(DLDCC) $(LIBDIRS) $^ -o $@ 

arecord.oct : oct_arecord.o aaudio.o
	$(DLDCC) $(LIBDIRS) $^ -o $@ 

aplayrec.oct : oct_aplayrec.o aaudio.o
	$(DLDCC) -lpthread $^ -o $@ 

ainfo.oct : oct_ainfo.o aaudio.o
	$(DLDCC) $(LIBDIRS) $^ -o $@


clean:
	rm -f *.o *~ *.obj *.rsp

distclean:
	rm -f *.o *~ *.obj *.rsp *.oct
