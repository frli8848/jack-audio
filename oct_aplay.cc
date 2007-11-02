/***
*
* Copyright (C) 2007 Fredrik Lingvall
*
* This file is part of the ...
*
* The .... is free software; you can redistribute it and/or modify 
* it under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* The .... is distributed in the hope that it will be useful, but 
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
*
* You should have received a copy of the GNU General Public License
* along with the DREAM Toolbox; see the file COPYING.  If not, write to the 
* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
* 02110-1301, USA.
*
***/

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>

#include <alsa/asoundlib.h>

//
// Octave headers.
//

#include <octave/oct.h>

#include <octave/config.h>

#include <iostream>
using namespace std;

#include <octave/defun-dld.h>
#include <octave/error.h>
#include <octave/oct-obj.h>
#include <octave/pager.h>
#include <octave/symtab.h>
#include <octave/variables.h>

#define TRUE 1
#define FALSE 0

#define USE_ALSA_FLOAT

//
// Macros.
//

#ifdef CLAMP
#undef CLAMP
#endif
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#define mxGetM(N)   args(N).matrix_value().rows()
#define mxGetN(N)   args(N).matrix_value().cols()
#define mxIsChar(N) args(N).is_string()


/***
 * Name and date (of revisions):
 * 
 * Fredrik Lingvall 2007-10-31 : File created.
 *
 ***/


//
// typedef:s
//




//
// Function prototypes.
//




/***
 * 
 * Octave (oct) gateway function for APLAY.
 *
 ***/

DEFUN_DLD (aplay, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  [Y] = aplay(A).\n\
\n\
APLAY Computes one dimensional convolutions of the columns in the matrix A and the matrix (or vector) B.\n\
\n\
Input parameters:\n\
\n\
@copyright{2007-10-31 Fredrik Lingvall}.\n\
@seealso {play, record}\n\
@end deftypefn")
{
  double *A; 
  int err;
  unsigned int i,m,n;
  int channels,fs;
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames,oframes;
#ifdef USE_ALSA_FLOAT
  float *buffer;
#else
  short *buffer;
#endif
  char device[50];
  int  buflen;
  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";
  octave_value_list oct_retval; 

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  if ((nrhs < 1) || (nrhs > 3)) {
    error("arecord requires 1 to 3 input arguments!");
    return oct_retval;
  }

  if (nlhs > 1) {
    error("Too many output arguments for arecord !");
    return oct_retval;
  }

  //
  // The audio data (a frames x channels matrix).
  //

  const Matrix tmp0 = args(0).matrix_value();
  frames = tmp0.rows();		// Audio data length for each channel.
  channels = tmp0.cols();	// Number of channels.
  A = (double*) tmp0.fortran_vec();
    
  if (frames < 0) {
    error("The number of audio frames (rows in arg 1) must > 0!");
    return oct_retval;
  }
  
  if (channels < 0) {
    error("The number of channels (columns in arg 1) must > 0!");
    return oct_retval;
  }

  //
  // Sampling frequency.
  //

  if (nrhs > 1) {
    
    if (mxGetM(1)*mxGetN(1) != 1) {
      error("2nd arg (the sampling frequency) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp1 = args(1).matrix_value();
    fs = (int) tmp1.fortran_vec()[0];
    
    if (fs < 0) {
      error("Error in 2nd arg. The samping frequency must be > 0!");
      return oct_retval;
    }
  } else
    fs = 8000; // Default to 8 kHz.

  //
  // Audio device
  //

  if (nrhs > 2) {
    
    if (!mxIsChar(2)) {
      error("3rd arg (the audio device) must be a string !");
      return oct_retval;
    }
    
    std::string strin = args(2).string_value(); 
    buflen = strin.length();
    for ( n=0; n<=buflen; n++ ) {
      device[n] = strin[n];
    }
    device[buflen] = '\0';
    
  } else
      strcpy(device,"default"); 


  // Allocate buffer space.
#ifdef USE_ALSA_FLOAT
  buffer = (float*) malloc(frames*channels*sizeof(float));
#else
  buffer = (short*) malloc(frames*channels*sizeof(short));
#endif
  
  // Convert to interleaved audio data.
  for (n = 0; n < channels; n++) {
    for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
#ifdef USE_ALSA_FLOAT
      buffer[i] =  (float) CLAMP(A[m], -1.0,1.0);
#else
      buffer[i] =  (short) CLAMP(32768.0*A[m], -32768, 32767);
#endif
    }
  }

  // Open in blocking mode (0, SND_PCM_NONBLOCK, or SND_PCM_ASYNC).
  if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK,0)) < 0) {
    error("Playback open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  if ((err = snd_pcm_set_params(handle,
#ifdef USE_ALSA_FLOAT
				SND_PCM_FORMAT_FLOAT, 
#else
				SND_PCM_FORMAT_S16,
#endif
				SND_PCM_ACCESS_RW_INTERLEAVED,
				channels,
				fs,
				1,
				0)) < 0) {	/* 0.5sec */
    error("Playback set params error: %s\n", snd_strerror(err));
    snd_pcm_close(handle);
    return oct_retval;
  }

  oframes = snd_pcm_writei(handle, buffer, frames);

  if (oframes < 0)
    oframes = snd_pcm_recover(handle, oframes, 0);
  
  if (oframes < 0)
    printf("snd_pcm_writei failed: %s\n", snd_strerror(err));
  
  
  if (oframes > 0 && oframes < frames)
    printf("Short write (expected %li, wrote %li)\n", frames, oframes);
  
  snd_pcm_close(handle);
  free(buffer);
  
  return oct_retval;
  
}
