/***
 *
 * Copyright (C) 2009 Fredrik Lingvall
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

// $Revision$ $Date$ $LastChangedBy$

/***
 *
 * atrecord - ALSA Triggered Recording
 *
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
 * Octave (oct) gateway function for ATRECORD.
 *
 ***/

DEFUN_DLD (atrecord, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {} [Y] = atrecord(trigger_pars,frames,channels,fs,dev_name,hw_pars).\n\
\n\
ATRECORD Captures audio data, from the PCM device given by dev_name,\n\
using the Advanced Linux Sound Architecture (ALSA) audio library API.\n\
ATRECORD runs continuously until the level of the input audio stream\n\
exceeds the trigger_level, then frames/2 is captured before the trigger\n\
occurred and frames/2 after the trigger occurred. To make the triggering more\n\
robust trigger_frames number of frames are used to compute the trigger threshold.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item trigger_pars\n\
The trigger parameter vector: trigger_pars = [trigger_level,trigger_ch,trigger_frames];\n\
\n\
@table @code\n\
@item trigger_level\n\
The trigger threshold level (>=0 and <= 1.0).\n\
The threshold is computed using:\n\
\n\
if (trigger_level > sum(abs(triggerbuffer)/trigger_frames) ...\n\
\n\
where triggerbuffer is the vector of audio samples currently inside the trigger buffer.\n\
\n\
@item trigger_ch\n\
The trigger channel. Optional: defaults to 1 (1st channel).\n\
@item trigger_frames\n\
The number of frames to use for triggering. Optional: defaults to fs number of frames (= 1 second trigger buffer).\n\
@end table\n\
@item frames\n\
The number of frames (samples/channel). Defaults to 2*trigger_frames.\n\
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
\n\
@item hw_pars\n\
hw_pars = [period_size num_periods]. Note these parameters may be changed to allowed \n\
values for the particular PCM device. Defaults to hw_pars = [512 2].\n\
@end table\n\
\n\
Output parameters:\n\
\n\
@table @samp\n\
@item Y\n\
A frames x channels matrix containing the captured audio data.\n\
@end table\n\
\n\
@copyright{} 2009 Fredrik Lingvall.\n\
@seealso {aplay, arecord, aplayrec, ainfo, @indicateurl{http://www.alsa-project.org}}\n\
@end deftypefn")
{
  double *A,*Y, *t_par, trigger_level; 
  int err, verbose = 0;
  octave_idx_type i,n,m;
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames, trigger_frames;
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
  snd_pcm_uframes_t period_size, r_period_size;
  unsigned int num_periods,r_num_periods;
  snd_pcm_uframes_t buffer_size;
  // SW parameters.
  snd_pcm_uframes_t avail_min;
  snd_pcm_uframes_t start_threshold;
  snd_pcm_uframes_t stop_threshold;
  size_t  ringbuffer_position;
  int trigger_ch;

  const snd_pcm_channel_area_t *record_areas;

  octave_value_list oct_retval; 
  
  int nrhs = args.length ();

  // Check for proper input and output  arguments.

  if ((nrhs < 1) || (nrhs > 7)) {
    error("atrecord requires 1 to 5 input arguments!");
    return oct_retval;
  }

  if (nlhs > 1) {
    error("Too many output arguments for atrecord !");
    return oct_retval;
  }

  //
  // Number of channels (arg 3).
  //

  if (nrhs > 2) {
    
    if (mxGetM(2)*mxGetN(2) != 1) {
      error("3rd arg (number of channels) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp2 = args(2).matrix_value();
    channels = (int) tmp2.fortran_vec()[0];
    
    if (channels < 0) {
      error("Error in 3rd arg. The number of channels must be > 0!");
      return oct_retval;
    }
  } else
    channels = 2; // Default to two capture channels.
  
  //
  // Sampling frequency (arg 4).
  //

  if (nrhs > 3) {
    
    if (mxGetM(3)*mxGetN(3) != 1) {
      error("4th arg (the sampling frequency) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp3 = args(3).matrix_value();
    fs = (int) tmp3.fortran_vec()[0];
    
    if (fs < 0) {
      error("Error in 4th arg. The sampling frequency must be > 0!");
      return oct_retval;
    }
  } else
    fs = 44100; // Defaults to 44.1 kHz.

  //
  // The trigger parameters (arg 1).
  //

  // Note we must know the sampling freq. to set the default
  // trigger buffer length.
  
  // Check that arg 1 is a 3 element vector
  if (!((mxGetM(0)<=3 && mxGetN(0)==1) || (mxGetM(0)==1 && mxGetN(0)<=3))) {
    error("Argument 1 must be a 1 to 3 element vector!");
    return oct_retval;
  }
  const Matrix tmp0 = args(0).matrix_value();
  t_par = (double*) tmp0.fortran_vec();
  trigger_level  = t_par[0]; // The trigger level (should be between 0.0 and 1.0).
  trigger_ch     = ((int)  t_par[1]) - 1; // Trigger channel (1--channels).
  trigger_frames = (size_t) t_par[2]; // The length of the trigger buffer.

  if (trigger_level < 0.0 || trigger_level > 1.0) {
    error("Error in 1st arg! The trigger level must be >= 0 and <= 1.0!");
    return oct_retval;
  }
  
  if ( mxGetM(0)*mxGetN(0) >= 2) {
    
    if (trigger_ch < 0 || trigger_ch > channels-1) {
      error("Error in arg 1! The trigger channel must be >= 1 and <= %d!",channels);
      return oct_retval;
    }
  } else
    trigger_ch = 0; // Default to a 1st channel.
  
  //
  // Number of audio frames (arg 2).
  //

  // Note we must know the trigger buffer length to set the default
  // trigger buffer length.

  if (nrhs > 1) {

    if (mxGetM(1)*mxGetN(1) != 1) {
      error("2nd arg (number of audio frames) must be a scalar !");
      return oct_retval;
    }
    
    const Matrix tmp1 = args(1).matrix_value();
    frames = (int) tmp1.fortran_vec()[0];
    
    if (frames <= 0) {
      error("Error in 2rd arg. The number of audio frames must be > 0!");
      return oct_retval;
    }

    if (frames < trigger_frames) {
      error("Error in 2rd arg. The number of audio frames must be larger than the number of the trigger frames!");
      return oct_retval;
    }

  }  else 
    frames = 2*trigger_frames; // Defaults to 2*trigger_frames.
  
  //
  // Audio device (arg 5).
  //

  if (nrhs > 4) {
    
    if (!mxIsChar(4)) {
      error("5th arg (the audio device) must be a string !");
      return oct_retval;
    }
    
    std::string strin = args(4).string_value(); 
    buflen = strin.length();
    for ( n=0; n<=buflen; n++ ) {
      device[n] = strin[n];
    }
    device[buflen] = '\0';
    
  } else
      strcpy(device,"default"); 

  //
  // HW/SW parameters (arg 6).
  //
  
  if (nrhs > 5) {    
    
    if (mxGetM(5)*mxGetN(5) != 2) {
      error("6th arg must be a 2 element vector !");
      return oct_retval;
    }
    
    const Matrix tmp5 = args(5).matrix_value();
    hw_sw_par = (double*) tmp5.fortran_vec();
    
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

  // Setup the hardware parameters for the capture device.
  if (nrhs <= 4) {
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

  if ( (r_period_size != period_size) && (nrhs > 6) )
    printf("Note: Requested period size %d adjusted to %d.\n", (int) r_period_size, (int) period_size);
  
  if ( (r_num_periods != num_periods) &&  (nrhs > 6) )
    printf("Note: Requested number of periods %d adjusted to %d.\n", (int) r_num_periods, (int) num_periods);
  
  // Note: the current code works when using fewer channels than the pcm device have when
  // the data is non-interleved but it doesn't work for interleved data. 
  // TODO: Fix so one can have wanted_channels < channels on interleaved devices too.

  if ( is_interleaved() && (wanted_channels != channels) && (nrhs > 3) ) {
    printf("Note: Requested number of channels %d adjusted to %d.\n",wanted_channels,channels);
    wanted_channels = channels;
  }

  if ( !is_interleaved() && (wanted_channels > channels) && (nrhs > 3) ) {
    printf("Note: Requested number of channels %d adjusted to %d.\n",wanted_channels,channels);
    wanted_channels = channels;
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
    error("Unable to set software parameters. Bailing out!");
    snd_pcm_close(handle);
    return oct_retval;
  }

  sample_bytes = snd_pcm_format_width(format)/8; // Compute the number of bytes per sample.
  
  if (verbose)
    printf("Sample format width: %d [bits]\n",snd_pcm_format_width(format));
  
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
    ringbuffer_position = t_read_and_poll_loop(handle,record_areas,format,
					       fbuffer,frames,framesize,
					       channels,wanted_channels,
					       trigger_level, trigger_ch,trigger_frames);
    break;    
    
  case SND_PCM_FORMAT_S32:
    ringbuffer_position = t_read_and_poll_loop(handle,record_areas,format,
					       ibuffer,frames,framesize,
					       channels,wanted_channels,
					       trigger_level, trigger_ch,trigger_frames);
    break;
    
  case SND_PCM_FORMAT_S16:
    ringbuffer_position = t_read_and_poll_loop(handle,record_areas,format,
					       sbuffer,frames,framesize,
					       channels,wanted_channels,
					       trigger_level, trigger_ch,trigger_frames);
    break;
    
  default:
    ringbuffer_position = t_read_and_poll_loop(handle,record_areas,format,
					       sbuffer,frames,framesize,
					       channels,wanted_channels,
					       trigger_level, trigger_ch,trigger_frames);
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
    
    octave_stdout << " frames = " << frames << " ch =  " <<  wanted_channels << "\n";
    // Allocate space for output data.
    Matrix Ymat(frames,wanted_channels);
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
	    Y[m] = ((double) ibuffer[i]) / 2147483648.0; // Normalize audio data.
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
      for (n = 0; n < frames*wanted_channels; n++) {
	
	switch(format) {
	  
	case SND_PCM_FORMAT_FLOAT:
	  Y[n] = (double) fbuffer[n];  
	  break;    
	  
	case SND_PCM_FORMAT_S32:
	  Y[n] = ((double) ibuffer[n]) / 2147483648.0; // Normalize audio data.
	  break;
	  
	case SND_PCM_FORMAT_S16:
	  Y[n] = ((double) sbuffer[n]) / 32768.0; // Normalize audio data.
	  break;
	  
	default:
	  Y[n] = ((double) sbuffer[n]) / 32768.0; // Normalize audio data.
	}
      }
    } 
    
    // Now shift the (ring) buffer so that the data is sequential in time.
    // That is, the last acquired frame should be at the end of the buffer
    // and the oldest frame should be first. The 'ringbuffer_position' is
    // the index of the last frame in the ring buffer. 

    if (ringbuffer_position > 0) {

      octave_stdout << "shifting ring buffer " << " : pos = " << ringbuffer_position << "\n";

      double *tmp_data;
      tmp_data = (double*) malloc(ringbuffer_position*1*sizeof(double));

      // Shift one channel each time.
      for (n=0; n<wanted_channels; n++) {
	
	memcpy(tmp_data, &Y[0 + n*frames], ringbuffer_position*1*sizeof(double));
	
	memmove(&Y[0 + n*frames], &Y[ringbuffer_position + n*frames],
		(frames - ringbuffer_position)*1*sizeof(double));
	
	memcpy(&Y[(frames - ringbuffer_position) + n*frames], tmp_data,
	       ringbuffer_position*1*sizeof(double));
      }
      free(tmp_data);
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
