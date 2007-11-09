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
 * Fredrik Lingvall 2007-11-09 : Switched to polling the audio devices.
 *
 ***/


//
// typedef:s
//

typedef struct
{
  snd_pcm_t *handle_rec;
  void *buffer_rec;
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
  octave_idx_type i,m,n;
  snd_pcm_t *handle_play,*handle_rec;
  snd_pcm_sframes_t frames;
  unsigned int framesize;
  unsigned int sample_bytes_play;
  unsigned int sample_bytes_rec;
  sighandler_t old_handler, old_handler_abrt, old_handler_keyint;
  pthread_t *threads;
  DATA   *D;
  void   *retval;

  float *fbuffer_play;
  int *ibuffer_play;
  short *sbuffer_play;

  float *fbuffer_rec;
  int *ibuffer_rec;
  short *sbuffer_rec;
  
  char device[50];
  int  buflen;

  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";

  // HW parameters
  snd_pcm_format_t format;
  unsigned int fs;
  unsigned int play_channels, rec_channels, wanted_play_channels, wanted_rec_channels;
  snd_pcm_uframes_t period_size;
  unsigned int num_periods;
  snd_pcm_uframes_t buffer_size;
  
  // SW parameters.
  snd_pcm_uframes_t avail_min;
  snd_pcm_uframes_t start_threshold;
  snd_pcm_uframes_t stop_threshold;

  octave_value_list oct_retval; 

  int nrhs = args.length ();

  running = FALSE;

  // Check for proper input and output  arguments.

  if ((nrhs < 1) || (nrhs > 4)) {
    error("aplayrec requires 1 to 4 input arguments!");
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
  play_channels = tmp0.cols();	// Number of channels.
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
  // Number of capture channels.
  //

  if (nrhs > 1) {
    
    if (mxGetM(1)*mxGetN(1) != 1) {
      error("2nd arg (number of channels) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp1 = args(1).matrix_value();
    rec_channels = (int) tmp1.fortran_vec()[0];
    
    if (rec_channels < 0) {
      error("Error in 1st arg. The number of channels must > 0!");
      return oct_retval;
    }
  } else
    channels = 2; // Default to two capture channels.

  //
  // Sampling frequency.
  //

  if (nrhs > 2) {
    
    if (mxGetM(2)*mxGetN(2) != 1) {
      error("3rd arg (the sampling frequency) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp2 = args(2).matrix_value();
    fs = (int) tmp2.fortran_vec()[0];
    
    if (fs < 0) {
      error("Error in 3rd arg. The samping frequency must be > 0!");
      return oct_retval;
    }
  } else
    fs = 44100; // Default to 44.1 kHz.

  //
  // Audio device.
  //

  if (nrhs > 3) {
    
    if (!mxIsChar(3)) {
      error("4th arg (the audio playback and capture device) must be a string !");
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


  //
  // HW/SW parameters
  //
  
  if (nrhs > 5) {    
    
    if (mxGetM(5)*mxGetN(5) != 5) {
      error("5th arg must be a 5 element vector !");
      return oct_retval;
    }
    
    const Matrix tmp5 = args(5).matrix_value();
    hw_sw_par = (double*) tmp3.fortran_vec();
    
    // hw parameters.
    period_size = (int) hw_sw_par[0];
    num_periods = (int) hw_sw_par[1];
    
    // sw parameters.
    avail_min = (int) hw_sw_par[2];
    start_threshold = (int) hw_sw_par[3];
    stop_threshold = (int) hw_sw_par[4];
  } 


//******************************************************************************************

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
  // Open and configure the PCM playback device. 
  //

  if ((err = snd_pcm_open(&handle_play,device,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)) < 0) {
    error("Playback open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  // Setup the hardwear parameters for the playback device.
  if (nrhs <= 4) {
    period_size = 512;
    num_periods = 1;
    //period_size = 16;
    //num_periods = 2;
  }
  format = SND_PCM_FORMAT_FLOAT; // Try to use floating point format.
  if (set_hwparams(handle_play,&format,&fs,&channels,&period_size,&num_periods,&buffer_size) < 0) {
    error("Unable to set audio playback hardware parameters. Bailing out!");
    snd_pcm_close(handle_play);
    return oct_retval;
  }
  
  // If the number of wanted_channels (given by input data) < channels (which depends on hardwear)
  // then we must append (silent) channels to get the right offsets (and avoid segfaults) when we 
  // copy data to the interleaved buffer. Another solution is just to print an error message and bail
  // out. 
  if (wanted_channels < channels) {
    error("You must have (at least) %d output channels for the used hardware!\n", channels);
    snd_pcm_close(handle_play);
    return oct_retval;
  }

  // Allocate playback buffer space.
  switch(format) {
    
  case SND_PCM_FORMAT_FLOAT:
    fbuffer_play = (float*) malloc(frames*channels*sizeof(float));
    break;    
    
  case SND_PCM_FORMAT_S32:
    ibuffer_play = (int*) malloc(frames*channels*sizeof(int));
    break;

  case SND_PCM_FORMAT_S16:
    sbuffer_play = (short*) malloc(frames*channels*sizeof(short));
    break;

  default:
    sbuffer_play = (short*) malloc(frames*channels*sizeof(short));
  }

  if (nrhs <= 4) {
    avail_min = period_size; // aplay uses this setting. 
    start_threshold = (play_buffer_size/avail_min) * avail_min;
    stop_threshold = 16*period_size; // Not sure what to use here.
  }

  if (set_swparams(handle_play,avail_min,start_threshold,stop_threshold) < 0) {
    error("Unable to set audio playback sofware parameters. Bailing out!");
    snd_pcm_close(handle_play);
    return oct_retval;
  }

  //
  // Open and configure the PCM capture device. 
  //

  if ((err = snd_pcm_open(&handle_rec, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    error("Capture open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  format = SND_PCM_FORMAT_FLOAT; // Try to use floating point format.
  if (set_hwparams(handle_rec,&format,&fs,&rec_channels,&period_size,&num_periods,&rec_buffer_size) < 0) {
    error("Unable to set audio capture hardware parameters. Bailing out!");
    snd_pcm_close(handle_rec);
    return oct_retval;
  }

  // If the number of wanted_channels < channels (which depends on hardwear)
  // then we must append (silent) channels to get the right offsets (and avoid segfaults) when we 
  // copy data to the interleaved buffer. Another solution is just to print an error message and bail
  // out. 
  if (wanted_rec_channels < rec_channels) {
    error("You must have (at least) %d input channels for the used hardware!\n", rec_channels);
    snd_pcm_close(handle_rec);
    return oct_retval;
  }

  // Allocate buffer space.
  switch(format) {
  
  case SND_PCM_FORMAT_FLOAT:
    fbuffer_rec = (float*) malloc(frames*channels*sizeof(float));
    break;    
    
  case SND_PCM_FORMAT_S32:
    ibuffer_rec = (int*) malloc(frames*channels*sizeof(int));
    break;

  case SND_PCM_FORMAT_S16:
    sbuffer_rec = (short*) malloc(frames*channels*sizeof(short));
    break;
    
  default:
    sbuffer_rec = (short*) malloc(frames*channels*sizeof(short));
  }
  
  if (nrhs <= 4) {
    avail_min = period_size; // aplay uses this setting. 
    start_threshold = (rec_buffer_size/avail_min) * avail_min;
    stop_threshold = 16*period_size;
  }
  
  if (set_swparams(handle,avail_min,start_threshold,stop_threshold) < 0) {
    error("Unable to set audio capture sofware parameters. Bailing out!");
    snd_pcm_close(handle);
    return oct_retval;
  }



  // Allocate memory for the capture (record) thread.
  threads = (pthread_t*) malloc(sizeof(pthread_t));
  if (!threads) {
    error("Failed to allocate memory for threads!");
    return oct_retval;
  }
  





  // Note done here!!!!



  //
  // Initialize and start the capture  thread.
  //

  D = (DATA*) malloc(sizeof(DATA));
  if (!D) {
    error("Failed to allocate memory for thread data!");
    return oct_retval;
  }


 //
  // Read the audio data from the PCM device.
  //

  switch(format) {
    
  case SND_PCM_FORMAT_FLOAT:
    read_and_poll_loop(handle,record_areas,format,fbuffer,frames,framesize);
    break;    
    
  case SND_PCM_FORMAT_S32:
    read_and_poll_loop(handle,record_areas,format,ibuffer,frames,framesize);
    break;
    
  case SND_PCM_FORMAT_S16:
    read_and_poll_loop(handle,record_areas,format,sbuffer,frames,framesize);
    break;
    
  default:
    read_and_poll_loop(handle,record_areas,format,sbuffer,frames,framesize);
  }
  

  
  // Init local data.
  D[0].handle_rec = handle_rec; 
  D[0].buffer_rec = buffer_rec;
  D[0].frames = frames;
  D[0].channels = rec_channels;

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
