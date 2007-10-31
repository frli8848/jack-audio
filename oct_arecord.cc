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

//
// Macros.
//

#ifdef CLAMP
#undef CLAMP
#endif
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))


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
  unsigned int i,m,n;
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames;
  short *buffer;
  char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";
  octave_value_list oct_retval; 

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  switch (nrhs) {
    
  case 0:
    error("arecord requires 1 to 5 input arguments!");
    return oct_retval;
    break;
    
  case 1:
    break;
    
  default:
    error("arecord requires 1 to 5 input arguments!");
    return oct_retval;
    break;
  }

  const Matrix tmp = args(0).matrix_value();
  A_M = tmp.rows(); // Audio data length.
  A_N = tmp.cols(); // Number of channels.
  A = (double*) tmp.fortran_vec();

  printf("A_M (frames) = %d A_N (channels) = %d\n",A_M,A_N);

  //
  // Call the play subroutine.
  //

  buffer = (short*) malloc(A_M*A_N*sizeof(short));



  //
  // Open audio device for capture.
  //
  
  if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    error("Capture open error: %s\n", snd_strerror(err));
    return oct_retval;
  }
  
  if ((err = snd_pcm_set_params(handle,
				SND_PCM_FORMAT_S16,
				SND_PCM_ACCESS_RW_INTERLEAVED,
				A_N,
				44100,
				1,
				0)) < 0) {	/* 0.5sec */
    error("Capture open error: %s\n", snd_strerror(err));
    return oct_retval;
  }
  
  frames = snd_pcm_readi(handle, buffer, A_M);
  printf("frames=%d\n",frames);

  if (frames < 0)
    frames = snd_pcm_recover(handle, frames, 0);
  
  if (frames < 0)
    printf("snd_pcm_readi failed: %s\n", snd_strerror(err));
  
  //if (frames > 0 && frames < (long) sizeof(buffer))
  //  printf("Short write (expected %li, wrote %li)\n", (long)sizeof(buffer), frames);


  Matrix Ymat(A_M,A_N);
  Y = Ymat.fortran_vec();

  // Convert from interleaved audio data.
  for (n = 0; n < A_N; n++) {
    for (i = n,m = n*A_M; m < (n+1)*A_M; i+=A_N,m++) {// n:th channel.
      Y[m] = ((double) buffer[i]) / 32768.0 ;
    }
  }

  oct_retval.append(Ymat);
  
  snd_pcm_close(handle);
  free(buffer);
  
  //playit(A);
  
  return oct_retval;
  
}
