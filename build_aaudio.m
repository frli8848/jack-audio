%
% Script to build the ALSA
%


% Octave
mkoctfile -v aaudio.cc -c 
mkoctfile -v oct_aplay.cc    aaudio.o -lasound -o aplay.oct
mkoctfile -v oct_arecord.cc  aaudio.o -lasound -o arecord.oct
mkoctfile -v oct_aplayrec.cc aaudio.o -lasound -lpthread -o arecord.oct
mkoctfile -v oct_ainfo.cc    aaudio.o -lasound  -o ainfo.oct
