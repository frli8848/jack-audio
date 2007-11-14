/***
 *
 * Copyright (C) 2007 Fredrik Lingvall
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with the program; see the file COPYING.  If not, write to the 
 *   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *   02110-1301, USA.
 *
 ***/

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>

#include <alsa/asoundlib.h>

#include "aaudio.h"

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
 * Fredrik Lingvall 2007-11-12 : Added CTRL-C support.
 * Fredrik Lingvall 2007-11-13 : Added non-interleaved support.
 *
 ***/

//
// typedef:s
//

typedef struct
{
  snd_pcm_t *handle;
  void *buffer;
  const snd_pcm_channel_area_t *areas;
  snd_pcm_format_t format;
  unsigned int frames;
  unsigned int framesize;
  unsigned int channels;
} DATA;

//
// Globals
//


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
 * Audio read (capture) thread function. 
 *
 ***/

void* smp_process(void *arg)
{
  DATA D = *(DATA *)arg;
  snd_pcm_t *handle_rec = D.handle;
  void *buffer_rec = D.buffer;
  const snd_pcm_channel_area_t *record_areas = D.areas;
  snd_pcm_format_t format = D.format;
  int frames = D.frames;
  int framesize = D.framesize;
  int channels = D.channels;

  read_and_poll_loop(handle_rec,record_areas,format,buffer_rec,frames,framesize,channels);

  return NULL;
}

/***
 *
 * Signal handlers.
 *
 ***/

void sighandler(int signum) {
  //printf("Caught signal SIGTERM.\n");
  clear_running_flag();
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
@deftypefn {Loadable Function} {}  [Y] = aplayrec(A,rec_channels,fs,dev_name).\n\
\n\
APLAYREC Plays audio data from the input matrix A, on the PCM device given by dev_name, and \n\
records audio data to the output matrix Y using the Advanced Linux Sound Architecture (ALSA)\n\
audio library API.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item A\n\
A frames x number of playback channels matrix.\n\
\n\
@item rec_channel\n\
The number of capture channels (default is 2).\n\
\n\
@item fs\n\
The sampling frequency in Hz (default is 44100 [Hz]).\n\
\n\
@item dev_name\n\
The ALSA device name, for example, 'hw:0,0', 'hw:1,0', 'plughw:0,0', 'default', etc.\n\
(defaults to 'default').\n\
@end table\n\
\n\
Output parameters:\n\
\n\
@table @samp\n\
@item Y\n\
A frames x rec_channels matrix containing the captured audio data.\n\
@end table\n\
\n\
@copyright{ 2007 Fredrik Lingvall}.\n\
@seealso {aplay, arecord, ainfo, @indicateurl{http://www.alsa-project.org}}\n\
@end deftypefn")
{
  double *A,*Y; 
  int A_M,A_N;
  int err, verbose = 0;
  octave_idx_type i,m,n;
  snd_pcm_t *handle_play,*handle_rec;
  snd_pcm_sframes_t frames;
  unsigned int play_framesize, rec_framesize;
  unsigned int sample_bytes;
  sighandler_t old_handler, old_handler_abrt, old_handler_keyint;
  pthread_t *threads;
  DATA  *D;
  void  *retval;
  float *fbuffer_play;
  int   *ibuffer_play;
  short *sbuffer_play;
  float *fbuffer_rec;
  int   *ibuffer_rec;
  short *sbuffer_rec;
  const snd_pcm_channel_area_t *record_areas;
  const snd_pcm_channel_area_t *play_areas;
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
  snd_pcm_uframes_t play_buffer_size;
  snd_pcm_uframes_t rec_buffer_size;
  // SW parameters.
  snd_pcm_uframes_t avail_min;
  snd_pcm_uframes_t start_threshold;
  snd_pcm_uframes_t stop_threshold;

  double *hw_sw_par;

  octave_value_list oct_retval; 

  int nrhs = args.length ();

  //
  // Check for proper number input and output arguments.
  //

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
  wanted_play_channels = play_channels;

  A = (double*) tmp0.fortran_vec();
    
  if (frames < 0) {
    error("The number of audio frames (rows in arg 1) must > 0!");
    return oct_retval;
  }
  
  if (play_channels < 0) {
    error("The number of playback channels (columns in arg 1) must > 0!");
    return oct_retval;
  }

  //
  // Number of capture channels.
  //

  if (nrhs > 1) {
    
    if (mxGetM(1)*mxGetN(1) != 1) {
      error("2nd arg (number of capure channels) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp1 = args(1).matrix_value();
    rec_channels = (int) tmp1.fortran_vec()[0];
    
    if (rec_channels < 0) {
      error("Error in 2nd arg. The number of capture channels must > 0!");
      return oct_retval;
    }
  } else
    rec_channels = 2; // Default to two capture channels.

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
    hw_sw_par = (double*) tmp5.fortran_vec();
    
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
  if (set_hwparams(handle_play,&format,&fs,&play_channels,&period_size,&num_periods,&play_buffer_size) < 0) {
    error("Unable to set audio playback hardware parameters. Bailing out!");
    snd_pcm_close(handle_play);
    return oct_retval;
  }
  
  // If the number of wanted_channels (given by input data) < channels (which depends on hardwear)
  // then we must append (silent) channels to get the right offsets (and avoid segfaults) when we 
  // copy data to the interleaved buffer. Another solution is just to print an error message and bail
  // out. 
  if (wanted_play_channels < play_channels) {
    error("You must have (at least) %d output channels for the used hardware!\n", play_channels);
    snd_pcm_close(handle_play);
    return oct_retval;
  }

  // Allocate playback buffer space.
  switch(format) {
    
  case SND_PCM_FORMAT_FLOAT:
    fbuffer_play = (float*) malloc(frames*play_channels*sizeof(float));
    break;    
    
  case SND_PCM_FORMAT_S32:
    ibuffer_play = (int*) malloc(frames*play_channels*sizeof(int));
    break;

  case SND_PCM_FORMAT_S16:
    sbuffer_play = (short*) malloc(frames*play_channels*sizeof(short));
    break;

  default:
    sbuffer_play = (short*) malloc(frames*play_channels*sizeof(short));
  }

  if (is_interleaved()) {
    
    // Convert to interleaved audio data.
    for (n = 0; n < play_channels; n++) {
      for (i = n,m = n*frames; m < (n+1)*frames; i+=play_channels,m++) {// n:th channel.
	
	switch(format) {
	  
	case SND_PCM_FORMAT_FLOAT:
	  fbuffer_play[i] =  (float) CLAMP(A[m], -1.0,1.0);
	  break;    
	  
	case SND_PCM_FORMAT_S32:
	  ibuffer_play[i] =  (int) CLAMP(214748364.0*A[m], -2147483648.0, 2147483647.0);
	  break;
	  
	case SND_PCM_FORMAT_S16:
	  sbuffer_play[i] =  (short) CLAMP(32768.0*A[m], -32768.0, 32767.0);
	  break;
	  
	default:
	  sbuffer_play[i] =  (short) CLAMP(32768.0*A[m], -32768.0, 32767.0);
	}
      }
    }
  } else { // Non-interleaved
    for (n = 0; n < frames*play_channels; n++) {
      
      switch(format) {
	
      case SND_PCM_FORMAT_FLOAT:
	fbuffer_play[n] =  (float) CLAMP(A[n], -1.0,1.0);
	break;    
	
      case SND_PCM_FORMAT_S32:
	ibuffer_play[n] =  (int) CLAMP(214748364.0*A[n], -2147483648.0, 2147483647.0);
	break;
	
      case SND_PCM_FORMAT_S16:
	sbuffer_play[n] =  (short) CLAMP(32768.0*A[n], -32768.0, 32767.0);
	break;
	
      default:
	sbuffer_play[n] =  (short) CLAMP(32768.0*A[n], -32768.0, 32767.0);
      }
    }
  }

  if (nrhs <= 4) {
    avail_min = period_size; // The aplay app uses this setting. 
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

  if ((err = snd_pcm_open(&handle_rec, device, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
    error("Capture open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  format = SND_PCM_FORMAT_FLOAT; // Try to use floating point format.
  wanted_rec_channels = rec_channels;
  if (set_hwparams(handle_rec,&format,&fs,&rec_channels,&period_size,&num_periods,&rec_buffer_size) < 0) {
    error("Unable to set audio capture hardware parameters. Bailing out!");
    snd_pcm_close(handle_rec);
    return oct_retval;
  }

  if (wanted_rec_channels < rec_channels) {
    printf("Warning: You must have (at least) %d input channels for the used hardware!\n", rec_channels);
    printf("Warning: %d input channels is now used!\n", rec_channels);

  }

  // Allocate buffer space.
  switch(format) {
  
  case SND_PCM_FORMAT_FLOAT:
    fbuffer_rec = (float*) malloc(frames*rec_channels*sizeof(float));
    break;    
    
  case SND_PCM_FORMAT_S32:
    ibuffer_rec = (int*) malloc(frames*rec_channels*sizeof(int));
    break;

  case SND_PCM_FORMAT_S16:
    sbuffer_rec = (short*) malloc(frames*rec_channels*sizeof(short));
    break;
    
  default:
    sbuffer_rec = (short*) malloc(frames*rec_channels*sizeof(short));
  }
  
  if (nrhs <= 4) {
    avail_min = period_size; // Tha aplay app uses this setting. 
    start_threshold = (rec_buffer_size/avail_min) * avail_min;
    stop_threshold = 16*period_size; // Perhaps use default here instead!
  }
  
  if (set_swparams(handle_rec,avail_min,start_threshold,stop_threshold) < 0) {
    error("Unable to set audio capture sofware parameters. Bailing out!");
    snd_pcm_close(handle_rec);
    return oct_retval;
  }
  
  //
  // Get the framesize from PCM device.
  //

  sample_bytes = snd_pcm_format_width(format)/8; // Compute the number of bytes per sample.

  if (verbose)
    printf("Sample format width: %d [bits]\n",snd_pcm_format_width(format));

  // Check if the hardware are using less then 32 bits.
  if ((format == SND_PCM_FORMAT_S32) && (snd_pcm_format_width(format) != 32))
    sample_bytes = 32/8; // Use int to store, for example, data for 24 bit cards. 

  play_framesize = play_channels * sample_bytes; // Compute the framesize;
  rec_framesize = rec_channels * sample_bytes; // Compute the framesize;

  //
  // Verbose status info.
  //

  if (verbose) {
    snd_output_t *snderr;
    snd_output_stdio_attach(&snderr ,stderr, 0);
    
    fprintf(stderr, "Playback state:%d\n", snd_pcm_state(handle_play));
    snd_pcm_dump_setup(handle_play, snderr);
    
    fprintf(stderr, "Record state:%d\n", snd_pcm_state(handle_rec));
    snd_pcm_dump_setup(handle_rec, snderr);
  }

  //
  // Initialize and start the capture thread.
  //

  // Allocate memory for the capture (record) thread.
  threads = (pthread_t*) malloc(sizeof(pthread_t));
  if (!threads) {
    error("Failed to allocate memory for threads!");
    return oct_retval;
  }

  D = (DATA*) malloc(sizeof(DATA));
  if (!D) {
    error("Failed to allocate memory for thread data!");
    return oct_retval;
  }
  
  // Init tread data parameters.
  D[0].handle = handle_rec; 
  D[0].areas = record_areas;
  D[0].format = format;
 switch(format) {
    
  case SND_PCM_FORMAT_FLOAT:
    D[0].buffer = fbuffer_rec; 
    break;    
    
  case SND_PCM_FORMAT_S32:
    D[0].buffer = ibuffer_rec; 
    break;
    
  case SND_PCM_FORMAT_S16:
    D[0].buffer = sbuffer_rec; 
    break;
    
  default:
    D[0].buffer = sbuffer_rec; 
  }
  D[0].frames = frames;
  D[0].framesize = rec_framesize;
  D[0].channels = rec_channels;

  // Set status to running (CTRL-C will clear the flag and stop play/capure).
  set_running_flag(); 

  // Start the read thread.
  err = pthread_create(&threads[0], NULL, smp_process, &D[0]);
  if (err != 0)
    error("Error when creating a new thread!\n");
  
  //
  // Write the audio data to the PCM device.
  //
  
  switch(format) {
    
  case SND_PCM_FORMAT_FLOAT:
    write_and_poll_loop(handle_play,play_areas,format,fbuffer_play,frames,play_framesize,play_channels);
    break;    
    
  case SND_PCM_FORMAT_S32:
    write_and_poll_loop(handle_play,play_areas,format,ibuffer_play,frames,play_framesize,play_channels);
    break;
    
  case SND_PCM_FORMAT_S16:
    write_and_poll_loop(handle_play,play_areas,format,sbuffer_play,frames,play_framesize,play_channels);
    break;
    
  default:
    write_and_poll_loop(handle_play,play_areas,format,sbuffer_play,frames,play_framesize,play_channels);
  }
  
  //
  // Wait for the read thread to finnish.
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
  
  if (!is_running()) { 
    error("CTRL-C pressed - playback and capture interrupted!\n"); // Bail out.
    
  } else {
    
    // Allocate space for output data.
    Matrix Ymat(frames,rec_channels);
    Y = Ymat.fortran_vec();

   if (is_interleaved()) {
      
      // Convert from interleaved audio data.
      for (n = 0; n < rec_channels; n++) {
	for (i = n,m = n*frames; m < (n+1)*frames; i+=rec_channels,m++) {// n:th channel.
	  
	  switch(format) {
	    
	  case SND_PCM_FORMAT_FLOAT:
	    Y[m] = (double) fbuffer_rec[i];  
	    break;    
	    
	  case SND_PCM_FORMAT_S32:
	    Y[m] = ((double) ibuffer_rec[i]) / 214748364.0; // Normalize audio data.
	    break;
	    
	  case SND_PCM_FORMAT_S16:
	    Y[m] = ((double) sbuffer_rec[i]) / 32768.0; // Normalize audio data.
	    break;
	    
	  default:
	    Y[m] = ((double) sbuffer_rec[i]) / 32768.0; // Normalize audio data.
	  }
	}
      }
    } else { // Non-interleaved
      for (n = 0; n < frames*rec_channels; n++) {
	
	switch(format) {
	  
	case SND_PCM_FORMAT_FLOAT:
	  Y[n] = (double) fbuffer_rec[n];  
	  break;    
	  
	case SND_PCM_FORMAT_S32:
	  Y[n] = ((double) ibuffer_rec[n]) / 214748364.0; // Normalize audio data.
	  break;
	  
	case SND_PCM_FORMAT_S16:
	  Y[n] = ((double) sbuffer_rec[n]) / 32768.0; // Normalize audio data.
	  break;
	  
	default:
	  Y[n] = ((double) sbuffer_rec[n]) / 32768.0; // Normalize audio data.
	}
      }
    } 
    
    oct_retval.append(Ymat);
  } // is_running

  //
  // Cleanup.
  //
  
  switch (format) {
    
  case SND_PCM_FORMAT_FLOAT:
    free(fbuffer_play);    
    free(fbuffer_rec);    
    break;    
    
  case SND_PCM_FORMAT_S32:
    free(ibuffer_play);
    free(ibuffer_rec);
    break;
    
  case SND_PCM_FORMAT_S16:
    free(sbuffer_play);
    free(sbuffer_rec);
    break;
    
  default:
    free(sbuffer_play);
    free(sbuffer_rec);
    
  }  
  snd_pcm_close(handle_play);
  snd_pcm_close(handle_rec);
  
  return oct_retval;
}
