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

#include "aaudio.h"

//
// Macros.
//

#define mxGetM(N)   args(N).matrix_value().rows()
#define mxGetN(N)   args(N).matrix_value().cols()
#define mxIsChar(N) args(N).is_string()

//
// Function prototypes.
//

void sighandler(int signum);
void sighandler(int signum);
void sig_abrt_handler(int signum);
void sig_keyint_handler(int signum);

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
 * Octave (oct) gateway function for ARECORD.
 *
 ***/

DEFUN_DLD (arecord, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  [Y] = arecord(frames,channels,fs,dev_name).\n\
\n\
ARECORD Captures audio data, from the PCM device given by dev_name,\n\
using the Advanced Linux Sound Architecture (ALSA) audio library API.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item frames\n\
The number of frames (samples/channel).\n\
\n\
@item channels\n\
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
A frames x channels matrix containing the captured audio data.\n\
@end table\n\
\n\
@copyright{ 2007 Fredrik Lingvall}.\n\
@seealso {aplay, aplayrec, ainfo, @indicateurl{http://www.alsa-project.org}}\n\
@end deftypefn")
{
  double *A,*Y; 
  int A_M,A_N;
  int err, verbose = 0;;
  octave_idx_type i,n,m;
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames;
  unsigned int framesize;
  unsigned int sample_bytes;
  float *fbuffer;
  int *ibuffer;
  short *sbuffer;
  sighandler_t old_handler, old_handler_abrt, old_handler_keyint;
  char device[50];
  int  buflen;
  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";

  double *hw_sw_par;

  // HW parameters
  snd_pcm_format_t format;
  unsigned int fs;
  unsigned int channels, wanted_channels;
  snd_pcm_uframes_t period_size;
  unsigned int num_periods;
  snd_pcm_uframes_t buffer_size;
  
  // SW parameters.
  snd_pcm_uframes_t avail_min;
  snd_pcm_uframes_t start_threshold;
  snd_pcm_uframes_t stop_threshold;

  const snd_pcm_channel_area_t *record_areas;

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
  frames = (int) tmp0.fortran_vec()[0];

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
    channels = (int) tmp1.fortran_vec()[0];
    
    if (channels < 0) {
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

  //
  // HW/SW parameters
  //
  
  if (nrhs > 4) {    
    
    if (mxGetM(4)*mxGetN(4) != 2) {
      error("5th arg must be a 2 element vector !");
      return oct_retval;
    }
    
    const Matrix tmp4 = args(4).matrix_value();
    hw_sw_par = (double*) tmp4.fortran_vec();
    
    // hw parameters.
    period_size = (int) hw_sw_par[0];
    num_periods = (int) hw_sw_par[1];
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
  // Open the PCM device for capture.
  //

  if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK)) < 0) {
    error("Capture open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  // Setup the hardwear parameters for the playback device.
  if (nrhs <= 4) {
    period_size = 512;
    num_periods = 2;
  }

  format = SND_PCM_FORMAT_FLOAT; // Try to use floating point format.
  wanted_channels = channels;
  if (set_hwparams(handle,&format,&fs,&channels,&period_size,&num_periods,&buffer_size) < 0) {
    error("Unable to set hardware parameters. Bailing out!");
    snd_pcm_close(handle);
    return oct_retval;
  }

  // If the number of wanted_channels < channels (which depends on hardwear)
  // then we must append (silent) channels to get the right offsets (and avoid segfaults) when we 
  // copy data to the interleaved buffer. Another solution is just to print an error message and bail
  // out. 
  if (wanted_channels < channels) {
    error("You must have (at least) %d input channels for the used hardware!\n", channels);
    snd_pcm_close(handle);
    return oct_retval;
  }

  // Allocate buffer space.
  switch(format) {
  
  case SND_PCM_FORMAT_FLOAT:
    fbuffer = (float*) malloc(frames*channels*sizeof(float));
    break;    
    
  case SND_PCM_FORMAT_S32:
    ibuffer = (int*) malloc(frames*channels*sizeof(int));
    break;

  case SND_PCM_FORMAT_S16:
    sbuffer = (short*) malloc(frames*channels*sizeof(short));
    break;
    
  default:
    sbuffer = (short*) malloc(frames*channels*sizeof(short));
  }

  avail_min = period_size; // aplay uses this setting. 
  start_threshold = (buffer_size/avail_min) * avail_min;
  stop_threshold = 16*period_size; // No idea what to set here.

  if (set_swparams(handle,avail_min,start_threshold,stop_threshold) < 0) {
    error("Unable to set sofware parameters. Bailing out!");
    snd_pcm_close(handle);
    return oct_retval;
  }

  sample_bytes = snd_pcm_format_width(format)/8; // Compute the number of bytes per sample.
  
  if (verbose)
    printf("Sample format width: %d [bits]\n",snd_pcm_format_width(format));
  
  // Check if the hardware are using less then 32 bits.
  if ((format == SND_PCM_FORMAT_S32) && (snd_pcm_format_width(format) != 32))
    sample_bytes = 32/8; // Use int to store, for example, data for 24 bit cards. 
  
  framesize = channels * sample_bytes; // Compute the framesize;

  //
  // Verbose status info.
  //

  if (verbose) {
    snd_output_t *snderr;
    snd_output_stdio_attach(&snderr ,stderr, 0);
    
    fprintf(stderr, "Record state:%d\n", snd_pcm_state(handle));
    snd_pcm_dump_setup(handle, snderr);
  }

  // Set status to running (CTRL-C will clear the flag and stop capture).
  set_running_flag(); 

  //
  // Read the audio data from the PCM device.
  //

  switch(format) {
    
  case SND_PCM_FORMAT_FLOAT:
    read_and_poll_loop(handle,record_areas,format,fbuffer,frames,framesize,channels);
    break;    
    
  case SND_PCM_FORMAT_S32:
    read_and_poll_loop(handle,record_areas,format,ibuffer,frames,framesize,channels);
    break;
    
  case SND_PCM_FORMAT_S16:
    read_and_poll_loop(handle,record_areas,format,sbuffer,frames,framesize,channels);
    break;
    
  default:
    read_and_poll_loop(handle,record_areas,format,sbuffer,frames,framesize,channels);
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
    error("CTRL-C pressed - audio capture interrupted!\n"); // Bail out.
  } else {
    
    // Allocate space for output data.
    Matrix Ymat(frames,channels);
    Y = Ymat.fortran_vec();
    
    if (is_interleaved()) {
      
      // Convert from interleaved audio data.
      for (n = 0; n < channels; n++) {
	for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
	  
	  switch(format) {
	    
	  case SND_PCM_FORMAT_FLOAT:
	    Y[m] = (double) fbuffer[i];  
	    break;    
	    
	  case SND_PCM_FORMAT_S32:
	    Y[m] = ((double) ibuffer[i]) / 214748364.0; // Normalize audio data.
	    break;
	    
	  case SND_PCM_FORMAT_S16:
	    Y[m] = ((double) sbuffer[i]) / 32768.0; // Normalize audio data.
	    break;
	    
	  default:
	    Y[m] = ((double) sbuffer[i]) / 32768.0; // Normalize audio data.
	  }
	}
      }
    } else { // Non-interleaved
      for (n = 0; n < frames*channels; n++) {
	
	switch(format) {
	  
	case SND_PCM_FORMAT_FLOAT:
	  Y[n] = (double) fbuffer[n];  
	  break;    
	  
	case SND_PCM_FORMAT_S32:
	  Y[n] = ((double) ibuffer[n]) / 214748364.0; // Normalize audio data.
	  break;
	  
	case SND_PCM_FORMAT_S16:
	  Y[n] = ((double) sbuffer[n]) / 32768.0; // Normalize audio data.
	  break;
	  
	default:
	  Y[n] = ((double) sbuffer[n]) / 32768.0; // Normalize audio data.
	}
      }
    } 
    
    oct_retval.append(Ymat);
    
  } // is_running.
  
  //
  // Cleanup.
  //
  
  snd_pcm_close(handle);

  switch (format) {
    
  case SND_PCM_FORMAT_FLOAT:
    free(fbuffer);    
    break;    
    
  case SND_PCM_FORMAT_S32:
    free(ibuffer);
    break;
    
  case SND_PCM_FORMAT_S16:
    free(sbuffer);
    break;
    
  default:
    free(sbuffer);
    
  }
  
  return oct_retval;
}
