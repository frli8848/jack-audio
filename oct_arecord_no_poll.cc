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

// $Revision$ $Date$ $LastChangedBy$

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

#include <octave/oct.h>

#include <iostream>
using namespace std;

#include <octave/defun-dld.h>
#include <octave/error.h>

#include <octave/pager.h>
#include <octave/symtab.h>
#include <octave/variables.h>

#define TRUE 1
#define FALSE 0

#define LATENCY 0
#define ALLOW_ALSA_RESAMPLE TRUE
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
 * Fredrik Lingvall 2007-11-01 : Added input arg checks.
 * Fredrik Lingvall 2007-11-02 : Added ALSA floating point support.
 *
 ***/


//
// typedef:s
//

#ifdef USE_ALSA_FLOAT
typedef float adata_type;
#else
typedef short adata_type;
#endif

//
// Function prototypes.
//

int xrun_recovery(snd_pcm_t *handle, int err);
int read_loop(snd_pcm_t *handle,
	       adata_type *samples,
	       int channels);

/***
 *
 *   Underrun and suspend recovery.
 *
 ***/
 
int xrun_recovery(snd_pcm_t *handle, int err)
{
  if (err == -EPIPE) {	/* under-run */
    err = snd_pcm_prepare(handle);
    if (err < 0)
      printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
    return 0;
  } else if (err == -ESTRPIPE) {
    while ((err = snd_pcm_resume(handle)) == -EAGAIN)
      sleep(1);	/* wait until the suspend flag is released */
    if (err < 0) {
      err = snd_pcm_prepare(handle);
      if (err < 0)
	printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
    }
    return 0;
  }
  return err;
}

/***
 *
 *   Transfer method - read only.
 * 
 ***/

int read_loop(snd_pcm_t *handle,
	       adata_type *adata,
	       int frames, int channels)
{
  double phase = 0;
  adata_type *ptr;
  int err, cptr;
  
  ptr = adata;
  cptr = frames;
  while (cptr > 0) {
    err = snd_pcm_readi(handle, ptr, cptr);

    if (err == -EAGAIN)
      continue;
    
    if (err < 0) {
      if (xrun_recovery(handle, err) < 0) {
	error("Write error: %s\n", snd_strerror(err));
	return -1;
      }
      //break;	/* skip one period */
    }
    ptr += err * channels;
    cptr -= err;
  }
  printf("hej %d\n",cptr);
}


/***
 * 
 * Octave (oct) gateway function for ARECORD.
 *
 ***/

DEFUN_DLD (arecord, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  [Y] = arecord(A).\n\
\n\
ARECORD Computes one dimensional convolutions of the columns in the matrix A and the matrix (or vector) B.\n\
\n\
Input parameters:\n\
\n\
@copyright{2007-10-31 Fredrik Lingvall}.\n\
@seealso {play, record}\n\
@end deftypefn")
{
  double *A,*Y; 
  int A_M,A_N;
  int err;
  int channels,fs;
  //unsigned int i,m,n;
  octave_idx_type i,m,n;
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames,oframes;
  adata_type *buffer;
  char device[50];
  int  buflen;
  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";
  octave_value_list oct_retval; 

  int nrhs = args.length ();

  // Check for proper input and output  arguments.

  if ((nrhs < 1) || (nrhs > 4)) {
    error("arecord requires 1 to 4 input arguments!");
    return oct_retval;
  }

  if (nlhs > 1) {
    error("Too many output arguments for arecord !");
    return oct_retval;
  }


  //
  // Number of audio frames.
  //

  if (mxGetM(0)*mxGetN(0) != 1) {
    error("1st arg (number of audio frames) must be a scalar !");
    return oct_retval;
  }

  const Matrix tmp0 = args(0).matrix_value();
  frames = (int) tmp0.data()[0];

  if (frames < 0) {
    error("Error in 1st arg. The number of audio frames must > 0!");
    return oct_retval;
  }

  //
  // Number of channels.
  //

  if (nrhs > 1) {
    
    if (mxGetM(1)*mxGetN(1) != 1) {
      error("2nd arg (number of channels) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp1 = args(1).matrix_value();
    channels = (int) tmp1.data()[0];
    
    if (channels < 0) {
      error("Error in 1st arg. The number of channels must > 0!");
      return oct_retval;
    }
  } else
    channels = 1; // Default to one channel.
  
  //
  // Sampling frequency.
  //

  if (nrhs > 2) {
    
    if (mxGetM(2)*mxGetN(2) != 1) {
      error("3rd arg (the sampling frequency) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp2 = args(2).matrix_value();
    fs = (int) tmp2.data()[0];
    
    if (fs < 0) {
      error("Error in 3rd arg. The samping frequency must be > 0!");
      return oct_retval;
    }
  } else
    fs = 8000; // Default to 8 kHz.

  //
  // Audio device
  //

  if (nrhs > 3) {
    
    if (!mxIsChar(3)) {
      error("4th arg (the audio device) must be a string !");
      return oct_retval;
    }
    
    std::string strin = args(3).string_value(); 
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

  //
  // Open audio device for capture.
  //
  
  if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    error("Capture open error: %s\n", snd_strerror(err));
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
				ALLOW_ALSA_RESAMPLE,
				LATENCY)) < 0) {
    error("Capture set params error: %s\n", snd_strerror(err));
    snd_pcm_close(handle);
    return oct_retval;
  }
  
  err = read_loop(handle,buffer,frames,channels);
  if (err < 0)
    printf("snd_pcm_readi failed: %s\n", snd_strerror(err));

  /*
  oframes = snd_pcm_readi(handle, buffer, frames);

  if (oframes < 0)
    oframes = snd_pcm_recover(handle, oframes, 0);
  
  if (oframes < 0)
    printf("snd_pcm_readi failed: %s\n", snd_strerror(err));
  
  if (oframes > 0 && oframes < frames)
    printf("Short read (expected %li, read %li)\n", frames, oframes);
  */

  // Allocate space for output data.
  Matrix Ymat(frames,channels);
  Y = (double*) Ymat.data();

  // Convert from interleaved audio data.
  for (n = 0; n < channels; n++) {
    for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
#ifdef USE_ALSA_FLOAT
      Y[m] = (double) buffer[i];
#else
      Y[m] = ((double) buffer[i]) / 32768.0; // Normalize audio data.
#endif
    }
  }

  oct_retval.append(Ymat);
  
  snd_pcm_close(handle);
  free(buffer);
  
  return oct_retval;
}
