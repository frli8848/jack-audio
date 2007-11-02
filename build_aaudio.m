%
% Script to build the ALSA
%


% Octave
mkoctfile -v oct_aplay.cc   -lasound -o aplay.oct
mkoctfile -v oct_arecord.cc -lasound -o arecord.oct
