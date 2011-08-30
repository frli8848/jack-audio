#!/usr/bin/make

# $Revision$ $Date$ $LastChangedBy

###################################
#
# File that defines compiler flags that is included
# in all Makefiles.
#
# Fredrik Lingvall 20071031 : File created.
#
###################################

# Compilers
CC		= gcc
CXX		= g++
DEBUGFLAGS	= #-g 


##############################################################
#
# Matlab release 14  issue: The Matlab libgcc_s.so seems to be buggy.
#
# Try setting LD_PRELOAD to your compiler's libgcc_s.so like, for example:
#
# export LD_PRELOAD=/usr/lib/gcc/i686-pc-linux-gnu/3.4.4/libgcc_s.so
#
##############################################################

MATLAB_FLAGS = -fexceptions -fPIC -pthread

# Location of mex.h
MEX_INCLUDE =  /usr/local/matlab/extern/include



##############################################################
#
# gcc flags 
#
##############################################################

# Standard (runs on all x86 machines)
CFLAGS	= -Wall -Wimplicit -pipe -O3  -march=pentium -funroll-loops -fomit-frame-pointer -mieee-fp

# Pentium III 
#CFLAGS = -Wall -Wimplicit -pipe -O3 -fomit-frame-pointer \
#	-funroll-loops -march=pentium3 -mtune=pentium3 -mfpmath=sse -mieee-fp

# Pentium 4 (Northwood)
#CFLAGS =  -Wall -Wimplicit -pipe -O1 -march=pentium4 -mtune=pentium4 \
#	-funroll-loops -fomit-frame-pointer -mfpmath=sse -mieee-fp


# Pentium 4 Prescott (with sse3 instructions).
#CFLAGS = -Wall -Wimplicit -pipe -O3 -march=prescott -mtune=prescott \
#	-funroll-loops -fomit-frame-pointer -mfpmath=sse -mieee-fp


# Pentium M (Mobile Centrino, e.g., Pentium 3 with sse2 instruction and P III's slow cache memory)
#CFLAGS = -Wall -pipe -O3 -march=pentium-m -mtune=pentium-m \
#	-funroll-loops -fomit-frame-pointer -mfpmath=sse -mieee-fp

# Pentium 4-M (Mobile)
#CFLAGS =  -Wall -Wimplicit -pipe  -O3 -march=pentium4m -mtune=pentium4m \
#	-funroll-loops -fomit-frame-pointer -mfpmath=sse -mieee-fp

# Pentium 4-M (mobile). 
#CFLAGS = -Wall -Wimplicit -pipe  -O1 -march=pentium4m -mtune=pentium4m \
#	-funroll-loops -fomit-frame-pointer -mfpmath=sse -mieee-fp

#
# Experimental: use -m64 and -DUSE_8_BYTE_INTS for 64-bit array dims and indexing. Note we probably need fftw >= 3.2.x
#

# AMD 64-bit Opteron
#CFLAGS = -m64 -Wall -Wimplicit -O3 -march=opteron -mtune=opteron \
#	-funroll-loops -fomit-frame-pointer -mfpmath=sse -mieee-fp

# Intel Nocona (sse2 and sse3). Also for Core2 until gcc 4.3 (which have -march=core2).  
#CFLAGS = -m64 -DUSE_8_BYTE_INTS -Wall -Wimplicit -O3 -march=nocona -mtune=nocona \
#	-funroll-loops -fomit-frame-pointer -mfpmath=sse -mieee-fp

# Platform dependent suffix of mex-files.
MEX_EXT = mexglx
 
OCTVER = 3.2.2
 
OCT_PATH = /usr/local
 

# x86 (I32)
#MEX_EXT = mexglx

# x86_64
#MEX_EXT = mexa64

# Solaris
#MEX_EXT = mexsol


#################################################
#
# Octave
#
#################################################


#
# Enter the Octave version here before building the toolbox!
#
#OCTVER = 2.9.13+

#
# Path to the octave.
#

#OCT_PATH = $(HOME)/work/NORTHWOOD
#OCT_PATH = $(HOME)/work/PRESCOTT
#OCT_PATH = $(HOME)/work/OPTERON

# Standard path.
#OCT_PATH = 

OCTAVE_HEADERS	= -I$(OCT_PATH)/include/octave-$(OCTVER) \
	-I$(OCT_PATH)/include/octave-$(OCTVER)/octave \
	-I$(OCT_PATH)/include/octave-$(OCTVER)/libcruft


# Octave libs.
OCT_LIBS	= -L$(OCT_PATH)/lib/octave-$(OCTVER) \
	-loctinterp -loctave -lcruft


