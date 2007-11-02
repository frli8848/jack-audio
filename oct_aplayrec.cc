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
 * Fredrik Lingvall 2007-11-02 : File created.
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
 * Octave (oct) gateway function for APLAYREC.
 *
 ***/

DEFUN_DLD (aplayrec, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  [Y] = aplayrec(A,fs,device).\n\
\n\
APLAYREC Computes one dimensional convolutions of the columns in the matrix A and the matrix (or vector) B.\n\
\n\
Input parameters:\n\
\n\
@copyright{2007 Fredrik Lingvall}.\n\
@seealso {aplay, arecord, play, record}\n\
@end deftypefn")
{
  double *A,*Y; 
  int A_M,A_N;
  int err;
  int channels,fs;
  //unsigned int i,m,n;
  octave_idx_type i,m,n;
  snd_pcm_t *handle_play,*handle_rec;
  snd_pcm_sframes_t frames,oframes_play,oframes_rec;
#ifdef USE_ALSA_FLOAT
  float *buffer_play;
  float *buffer_rec;
#else
  short *buffer_play;
  short *buffer_rec;
#endif
  char device[50];
  int  buflen;
  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";
  octave_value_list oct_retval; 

  int nrhs = args.length ();

  // Check for proper input and output  arguments.

  if ((nrhs < 1) || (nrhs > 3)) {
    error("aplayrec requires 1 to 3 input arguments!");
    return oct_retval;
  }

  if (nlhs > 1) {
    error("Too many output arguments for aplayrec!");
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
  // Audio device.
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

  //
  // Allocate buffer space. 
  //

#ifdef USE_ALSA_FLOAT
  buffer_play = (float*) malloc(frames*channels*sizeof(float));
  buffer_rec = (float*) malloc(frames*channels*sizeof(float));
#else
  buffer_play = (short*) malloc(frames*channels*sizeof(short));
  buffer_rec = (short*) malloc(frames*channels*sizeof(short));
#endif

  //
  // Open audio device for capture.
  //
  
  // Open in blocking mode (available modes are: 0, SND_PCM_NONBLOCK, or SND_PCM_ASYNC).
  if ((err = snd_pcm_open(&handle_rec, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    error("Capture open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  if ((err = snd_pcm_set_params(handle_rec,
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
    error("Capture set params error: %s\n", snd_strerror(err));
    snd_pcm_close(handle_rec);
    return oct_retval;
  }

  //
  // Open audio device for play.
  //

  // Open in non-blocking mode (available modes are: 0, SND_PCM_NONBLOCK, or SND_PCM_ASYNC).
  if ((err = snd_pcm_open(&handle_play, device, SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)) < 0) {
    error("Playback open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  if ((err = snd_pcm_set_params(handle_play,
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
    snd_pcm_close(handle_play);
    return oct_retval;
  }

  //
  // Convert to interleaved audio data and copy to output buffer.
  //
  
  for (n = 0; n < channels; n++) {
    for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
#ifdef USE_ALSA_FLOAT
      buffer_play[i] =  (float) CLAMP(A[m], -1.0,1.0);
#else
      buffer_play[i] =  (short) CLAMP(32768.0*A[m], -32768, 32767);
#endif
    }
  }
  
  //
  // Play audio data.
  //
  
  oframes_play = snd_pcm_writei(handle_play, buffer_play, frames);
  
  //
  // Read audio data.
  //

  oframes_rec = snd_pcm_readi(handle_rec, buffer_rec, frames);

  if (oframes_rec < 0)
    oframes_rec = snd_pcm_recover(handle_rec, oframes_rec, 0);
  
  if (oframes_rec < 0)
    printf("snd_pcm_readi failed: %s\n", snd_strerror(err));
  
  if (oframes_rec > 0 && oframes_rec < frames)
    printf("Short read (expected %li, read %li)\n", frames, oframes_rec);


  // Allocate space for output data.
  Matrix Ymat(frames,channels);
  Y = Ymat.fortran_vec();

  // Convert from interleaved audio data.
  for (n = 0; n < channels; n++) {
    for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
#ifdef USE_ALSA_FLOAT
      Y[m] = (double) buffer_rec[i];
#else
      Y[m] = ((double) buffer_rec[i]) / 32768.0; // Normalize audio data.
#endif
    }
  }

  oct_retval.append(Ymat);
  
  snd_pcm_close(handle_play);
  snd_pcm_close(handle_rec);
  free(buffer_play);
  free(buffer_rec);
  
  return oct_retval;
}
