/***
 *
 * Copyright (C) 2007, 2008, 2009 Fredrik Lingvall
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
#include <poll.h>

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

#ifdef CLAMP
#undef CLAMP
#endif
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

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
 * Octave (oct) gateway function for APLAY.
 *
 ***/

DEFUN_DLD (aplay, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {} aplay(A,fs,dev_name,hw_pars).\n\
\n\
APLAY Plays audio data from the input matrix A, on the PCM device given by dev_name,\n\
using the Advanced Linux Sound Architecture (ALSA) audio library API.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item A\n\
A frames x number of playback channels matrix.\n\
\n\
@item fs\n\
The sampling frequency in Hz (default is 44100 [Hz]).\n\
\n\
@item dev_name\n\
The ALSA device name, for example, 'hw:0,0', 'hw:1,0', 'plughw:0,0', 'default', etc.\n\
(defaults to 'default').\n\
\n\
@item hw_pars\n\
hw_pars = [period_size num_periods]. Note these parameters may be changed to allowed \n\
values for the particular PCM device. Defaults to hw_pars = [512 2].\n							\
@end table\n\
\n\
@copyright{} 2008,2009 Fredrik Lingvall.\n\
@seealso {arecord, aplayrec, ainfo, @indicateurl{http://www.alsa-project.org}}\n\
@end deftypefn")
{
  double *A; 
  int err,verbose = 0;
  octave_idx_type i,n,m;
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames;
  unsigned int framesize;
  unsigned int sample_bytes;
  sighandler_t old_handler, old_handler_abrt, old_handler_keyint;
  float *fbuffer;
  int *ibuffer;
  short *sbuffer;
  char device[50];
  int  buflen;
  const snd_pcm_channel_area_t *play_areas;
  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";
  double *hw_sw_par;
  // HW parameters
  snd_pcm_format_t format;
  unsigned int fs;
  unsigned int channels, wanted_channels;
  snd_pcm_uframes_t period_size, r_period_size;
  unsigned int num_periods,r_num_periods;
  snd_pcm_uframes_t buffer_size;
  // SW parameters.
  snd_pcm_uframes_t avail_min;
  snd_pcm_uframes_t start_threshold;
  snd_pcm_uframes_t stop_threshold;
  octave_value_list oct_retval; // Octave return (output) parameters

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  if ((nrhs < 1) || (nrhs > 4)) {
    error("aplay requires 1 to 4 input arguments!");
    return oct_retval;
  }

  if (nlhs > 0) {
    error("aplay don't have output arguments!");
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
      error("Error in 2nd arg. The sampling frequency must be > 0!");
      return oct_retval;
    }
  } else
    fs = 44100; // Default to 44.1 kHz.

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

  //
  // HW/SW parameters
  //
  
  if (nrhs > 3) {    
    
    if (mxGetM(3)*mxGetN(3) != 2) {
      error("4th arg must be a 2 element vector !");
      return oct_retval;
    }
    
    const Matrix tmp3 = args(3).matrix_value();
    hw_sw_par = (double*) tmp3.fortran_vec();
    
    // hw parameters.
    period_size = (int) hw_sw_par[0];
    num_periods = (int) hw_sw_par[1];
  } 

  //
  // Register signal handlers.
  //

  if ((old_handler = signal(SIGTERM, &sighandler)) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }

  if ((old_handler_abrt = signal(SIGABRT, &sighandler)) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }
  
  if ((old_handler_keyint = signal(SIGINT, &sighandler)) == SIG_ERR) {
    printf("Couldn't register signal handler.\n");
  }
  
  //
  // Open the PCM playback device. 
  //

  if ((err = snd_pcm_open(&handle,device,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)) < 0) {
    error("Playback open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  //
  // Setup the hardware parameters for the playback device.
  //

  if (nrhs <= 3) {
    period_size = 512;
    num_periods = 2;
  }
  
  r_period_size = period_size;
  r_num_periods = num_periods;
  format = SND_PCM_FORMAT_FLOAT; // Try to use floating point format.
  wanted_channels = channels;
  if (set_hwparams(handle,&format,&fs,&channels,&period_size,&num_periods,&buffer_size) < 0) {
    error("Unable to set hardware parameters. Bailing out!");
    snd_pcm_close(handle);
    return oct_retval;
  }

  if ( (r_period_size != period_size) && (nrhs > 3) )
    printf("Note: Requested period size %d adjusted to %d.\n",(int) r_period_size, (int) period_size);
  
  if ( (r_num_periods != num_periods) &&  (nrhs > 3) )
    printf("Note: Requested number of periods %d adjusted to %d.\n",r_num_periods,num_periods);


  // If the number of wanted_channels (given by input data) < channels (which depends on hardware)
  // then we must append (silent) channels to get the right offsets (and avoid segfaults) when we 
  // copy data to the interleaved buffer. Another solution is just to print an error message 
  // and bail out. 

  if (wanted_channels < channels) {
    error("You must have (at least) %d input channels for the used hardware!\n", channels);
    snd_pcm_close(handle);
    return oct_retval;
  }

  // Its safe to use more input channels the the hardware supports. That is, since Octave stores 
  // data column vise only the first input channels (columns in A) will be played on the hardware 
  // and hence  unallocated memory will never be accessed. Print a note that some channels are 
  // ignored.

  if (wanted_channels > channels) {
    printf("Note: Requested number of channels %d adjusted to %d.\n",wanted_channels,channels);
    printf("      Channel %d to %d is ignored.\n",channels+1,wanted_channels);
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

  if (is_interleaved()) {
    
    // Convert to interleaved audio data.
    for (n = 0; n < channels; n++) {
      for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
	
	switch(format) {
	  
	case SND_PCM_FORMAT_FLOAT:
	  fbuffer[i] =  (float) CLAMP(A[m], -1.0,1.0);
	  break;    
	  
	case SND_PCM_FORMAT_S32:
	  ibuffer[i] =  (int) CLAMP(2147483648.0*A[m], -2147483648.0, 2147483647.0);
	  break;
	  
	case SND_PCM_FORMAT_S16:
	  sbuffer[i] =  (short) CLAMP(32768.0*A[m], -32768.0, 32767.0);
	  break;
	  
	default:
	  sbuffer[i] =  (short) CLAMP(32768.0*A[m], -32768.0, 32767.0);
	}
      }
    }
  } else { // Non-interleaved
    for (n = 0; n < frames*channels; n++) {

      switch(format) {
	
      case SND_PCM_FORMAT_FLOAT:
	fbuffer[n] =  (float) CLAMP(A[n], -1.0,1.0);
	break;    
	
      case SND_PCM_FORMAT_S32:
	ibuffer[n] =  (int) CLAMP(2147483648.0*A[n], -2147483648.0, 2147483647.0);
	break;
	
      case SND_PCM_FORMAT_S16:
	sbuffer[n] =  (short) CLAMP(32768.0*A[n], -32768.0, 32767.0);
	break;
	
      default:
	sbuffer[n] =  (short) CLAMP(32768.0*A[n], -32768.0, 32767.0);
      }

    }
  }
  
  avail_min = period_size; // The aplay app uses this setting. 
  start_threshold = (buffer_size/avail_min) * avail_min;
  stop_threshold = 16*period_size; // No idea what to use here.
  
  if (set_swparams(handle,avail_min,start_threshold,stop_threshold) < 0) {
    error("Unable to set software parameters. Bailing out!");
    snd_pcm_close(handle);
    return oct_retval;
  }

  sample_bytes = snd_pcm_format_width(format)/8; // Compute the number of bytes per sample.

  if (verbose)
    octave_stdout << " Sample format width: " << snd_pcm_format_width(format) << " [bits]\n";
  //printf("Sample format width: %d [bits]\n",snd_pcm_format_width(format));

  
  // Check if the hardware are using less then 32 bits.
  if ((format == SND_PCM_FORMAT_S32) && (snd_pcm_format_width(format) != 32))
    sample_bytes = 32/8; // Use int to store, for example, data for 24 bit cards. 
  
  framesize = channels * sample_bytes; // Compute the frame size;

  //
  // Verbose status info.
  //

  if (verbose) {
    snd_output_t *snderr;
    snd_output_stdio_attach(&snderr ,stderr, 0);
    
    fprintf(stderr, "play_state:%d\n", snd_pcm_state(handle));
    snd_pcm_dump_setup(handle, snderr);
  }

  // Set status to running (CTRL-C will clear the flag and stop playback).
  set_running_flag(); 

  //
  // Write the audio data to the PCM device.
  //

  switch(format) {
    
  case SND_PCM_FORMAT_FLOAT:
    write_and_poll_loop(handle,play_areas,format,fbuffer,frames,framesize,channels);
    break;    
    
  case SND_PCM_FORMAT_S32:
    write_and_poll_loop(handle,play_areas,format,ibuffer,frames,framesize,channels);
    break;
    
  case SND_PCM_FORMAT_S16:
    write_and_poll_loop(handle,play_areas,format,sbuffer,frames,framesize,channels);
    break;
    
  default:
    write_and_poll_loop(handle,play_areas,format,sbuffer,frames,framesize,channels);
  }

  //
  // Restore old signal handlers.
  //
  
  if (signal(SIGTERM, old_handler) == SIG_ERR) {
    error("Couldn't register old signal handler.\n");
  }
  
  if (signal(SIGABRT,  old_handler_abrt) == SIG_ERR) {
    error("Couldn't register signal handler.\n");
  }
  
  if (signal(SIGINT, old_handler_keyint) == SIG_ERR) {
    error("Couldn't register signal handler.\n");
  }
  
  if (!is_running())
    error("CTRL-C pressed - playback interrupted!\n"); // Bail out.

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
  
  if (!is_running())
    error("CTRL-C pressed - playback interrupted!\n");
  
  return oct_retval;
}
