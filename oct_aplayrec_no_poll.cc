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
 * Fredrik Lingvall 2007-11-02 : File created.
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

typedef struct
{
  snd_pcm_t *handle_rec;
  adata_type *buffer_rec;
  int frames;
  int channels;
} DATA;

//
// Globals
//
volatile int running;


//
// Function prototypes.
//

void* smp_process(void *arg);
void sighandler(int signum);
void sighandler(int signum);
void sig_abrt_handler(int signum);
void sig_keyint_handler(int signum);

int xrun_recovery(snd_pcm_t *handle, int err);
int write_loop(snd_pcm_t *handle,
	       adata_type *samples,
	       int channels);

/***
 *
 * Audio read thread function. 
 *
 ***/

void* smp_process(void *arg)
{
  DATA D = *(DATA *)arg;
  snd_pcm_t *handle_rec = D.handle_rec;
  adata_type *buffer_rec = D.buffer_rec;
  int frames = D.frames;
  int channels = D.channels;
  adata_type *ptr;
  int err, cptr;
  
  ptr = buffer_rec;
  cptr = frames;
  while (cptr > 0) {
    err = snd_pcm_readi(handle_rec, ptr, cptr);

    if (err == -EAGAIN)
      continue;
    
    if (err < 0) {
      if (xrun_recovery(handle_rec, err) < 0) {
	error("Write error: %s\n", snd_strerror(err));
	return(NULL);
      }
      break;	/* skip one period */
    }
    ptr += err * channels;
    cptr -= err;

    if (running==FALSE) {
      printf("Read thread bailing bailing out!\n");
      break;
    }
  }

  return(NULL);
}

/***
 *
 * Signal handlers.
 *
 ***/

void sighandler(int signum) {
  //printf("Caught signal SIGTERM.\n");
  running = FALSE;
}

void sig_abrt_handler(int signum) {
  //printf("Caught signal SIGABRT.\n");
}

void sig_keyint_handler(int signum) {
  //printf("Caught signal SIGINT.\n");
}



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
 *   Transfer method - write only.
 * 
 ***/

int write_loop(snd_pcm_t *handle,
	       adata_type *adata,
	       int frames, int channels)
{
  double phase = 0;
  adata_type *ptr;
  int err, cptr;
  
  ptr = adata;
  cptr = frames;
  while (cptr > 0) {
    err = snd_pcm_writei(handle, ptr, cptr);

    if (err == -EAGAIN)
      continue;
    
    if (err < 0) {
      if (xrun_recovery(handle, err) < 0) {
	error("Write error: %s\n", snd_strerror(err));
	return -1;
      }
      break;	/* skip one period */
    }
    ptr += err * channels;
    cptr -= err;
  }
}




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
  sighandler_t   old_handler, old_handler_abrt, old_handler_keyint;
  pthread_t *threads;
  DATA   *D;
  void   *retval;

  adata_type *buffer_play;
  adata_type *buffer_rec;
  char device[50];
  int  buflen;
  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";
  octave_value_list oct_retval; 

  int nrhs = args.length ();

  running = FALSE;

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
  // Register signal handlers.
  //

  if ((old_handler = signal(SIGTERM, &sighandler)) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }

  if ((old_handler_abrt=signal(SIGABRT, &sighandler)) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }
  
  if ((old_handler_keyint=signal(SIGINT, &sighandler)) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }


  //
  // Allocate buffer space. 
  //

  buffer_play = (adata_type*) malloc(frames*channels*sizeof(adata_type));
  buffer_rec = (adata_type*) malloc(frames*channels*sizeof(adata_type));


  // Allocate mem for the thread.
  threads = (pthread_t*) malloc(sizeof(pthread_t));
  if (!threads) {
    error("Failed to allocate memory for threads!");
    return oct_retval;
  }
  
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
				ALLOW_ALSA_RESAMPLE,
				LATENCY)) < 0) {
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
				ALLOW_ALSA_RESAMPLE,
				LATENCY)) < 0) {
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
      buffer_play[i] =  (adata_type) CLAMP(A[m], -1.0,1.0);
#else
      buffer_play[i] =  (adata_type) CLAMP(32768.0*A[m], -32768, 32767);
#endif
    }
  }

  //
  // Start the read thread.
  //

  D = (DATA*) malloc(sizeof(DATA));
  if (!D) {
    error("Failed to allocate memory for thread data!");
    return oct_retval;
  }
  
  // Init local data.
  D[0].handle_rec = handle_rec; 
  D[0].buffer_rec = buffer_rec;
  D[0].frames = frames;
  D[0].channels = channels;

  running = TRUE;

  // Start the read thread.
  err = pthread_create(&threads[0], NULL, smp_process, &D[0]);
  if (err != 0)
    error("Error when creating a new thread!\n");

  //
  // Play audio data.
  //
  
  //oframes_play = snd_pcm_writei(handle_play, buffer_play, frames);
  err = write_loop(handle_play,buffer_play,frames,channels);
  if (err < 0)
    printf("snd_pcm_writei failed: %s\n", snd_strerror(err));
  
  //
  // Finnish the read thread..
  //

  // Wait for the read thread to finnish.
  err = pthread_join(threads[0], &retval);
  if (err != 0) {
    error("Error when joining a thread!\n");
    return oct_retval;
  }
  
  // Free memory.
  if (D) {
    free((void*) D);
  }

  //err = read_loop(handle_rec,buffer_rec,frames,channels);
  //if (err < 0)
  //  printf("snd_pcm_readi failed: %s\n", snd_strerror(err));

  /*
  oframes_rec = snd_pcm_readi(handle_rec, buffer_rec, frames);

  if (oframes_rec < 0)
    oframes_rec = snd_pcm_recover(handle_rec, oframes_rec, 0);
  
  if (oframes_rec < 0)
    printf("snd_pcm_readi failed: %s\n", snd_strerror(err));
  
  if (oframes_rec > 0 && oframes_rec < frames)
    printf("Short read (expected %li, read %li)\n", frames, oframes_rec);
  */

  //
  // Restore old signal handlers.
  //
  
  if (signal(SIGTERM, old_handler) == SIG_ERR) {
    printf("Couldn't register old signal handler.\n");
  }
  
  if (signal(SIGABRT,  old_handler_abrt) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }
  
  if (signal(SIGINT, old_handler_keyint) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }
  
  if (!running) {
    error("CTRL-C pressed!\n"); // Bail out.

  } else {
    
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
  }
  
  snd_pcm_close(handle_play);
  snd_pcm_close(handle_rec);
  free(buffer_play);
  free(buffer_rec);
  
  return oct_retval;
}
