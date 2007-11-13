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

#define TRUE 1
#define FALSE 0

#define LATENCY 0
#define ALLOW_ALSA_RESAMPLE TRUE

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
 * Fredrik Lingvall 2007-11-12 : File created.
 *
 ***/


//
// typedef:s
//


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
 * Octave (oct) gateway function for AINFO.
 *
 ***/

DEFUN_DLD (ainfo, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  ainfo(dev_name).\n\
\n\
AINFO Prints various hardware info of the PCM device given by dev_name\n\
using the Advanced Linux Sound Architecture (ALSA) audio library API.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
\n\
@item dev_name\n\
The ALSA device name, i.e., 'hw:0,0', 'plughw:0,0', or 'default' (defaults to 'default').\n\
@end table\n\
\n\
@copyright{ 2007 Fredrik Lingvall}.\n\
@seealso {aplay, arecord, aplayrec, @indicateurl{http://www.alsa-project.org}}\n\
@end deftypefn")
{
  snd_pcm_t *handle_play;
  snd_pcm_t *handle_rec;
  char device[50];
  int  buflen,n;
  snd_pcm_hw_params_t *hwparams_play, *hwparams_rec;
  snd_pcm_format_t tmp_format;
  unsigned int max1,min1;
  snd_pcm_uframes_t max2,min2;
  int dir, err;
  unsigned int val;
  snd_pcm_uframes_t val2;
  unsigned int tmp_fs;
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


  octave_value_list oct_retval; // Octave return (output) parameters

  int nrhs = args.length ();
  
  // Check for proper inputs arguments.
  
  if (nrhs > 1) {
    error("ainfo don't have more than one input argument!");
    return oct_retval;
  }
  
  if (nlhs > 0) {
    error("ainfo don't have output arguments!");
    return oct_retval;
  }
  
  //
  //  The audio device.
  //
  
  if (nrhs == 1) {
    
    if (!mxIsChar(0)) {
      error("1st arg (the audio device) must be a string !");
      return oct_retval;
    }
    
    std::string strin = args(0).string_value(); 
    buflen = strin.length();
    for (n=0; n<=buflen; n++ ) {
      device[n] = strin[n];
    }
    device[buflen] = '\0';
    
  } else
    strcpy(device,"default"); 
  
  
  //******************************************************************************************
  
  //
  // List all devices if no input arg is given.
  //
  
  if (nrhs < 1) {
    printf("No ALSA device was given. Listing the devices:\n\n");
    device_list();
    return oct_retval;
  }
  
  //
  // Open the PCM playback and capture devices. 
  //

  if ((err = snd_pcm_open(&handle_play,device,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)) < 0) {
    error("Playback open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  if ((err = snd_pcm_open(&handle_rec, device, SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK)) < 0) {
    error("Capture open error: %s\n", snd_strerror(err));
    return oct_retval;
  }
  
  //
  // Initialize the hw structure.
  //

  snd_pcm_hw_params_alloca(&hwparams_play);
  snd_pcm_hw_params_alloca(&hwparams_rec);
  
  if ((err = snd_pcm_hw_params_any(handle_play, hwparams_play)) < 0){
    fprintf(stderr,"Broken configuration: no configurations available: %s\n",
	    snd_strerror(err));
    snd_pcm_close(handle_play);
    snd_pcm_close(handle_rec);
    return oct_retval;
  }

  if ((err = snd_pcm_hw_params_any(handle_rec, hwparams_rec)) < 0){
    fprintf(stderr,"Broken configuration: no configurations available: %s\n",
	    snd_strerror(err));
    snd_pcm_close(handle_play);
    snd_pcm_close(handle_rec);
    return oct_retval;
  }
  
  //
  // Set read/write format to MMPAP:ed interleaved .
  //

  //if ((err = snd_pcm_hw_params_set_access(handle_play,hwparams_play,SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0){
  //  fprintf(stderr, "Unable to set the PCM access type: %s\n",
  //    snd_strerror(err));

  if ((err = snd_pcm_hw_params_set_access(handle_play,hwparams_play,SND_PCM_ACCESS_RW_NONINTERLEAVED)) < 0){
    fprintf(stderr, "Unable to set the PCM access type: %s\n",
	    snd_strerror(err));
    snd_pcm_close(handle_play);
    snd_pcm_close(handle_rec);
    return oct_retval;
  }

  if((err = snd_pcm_hw_params_set_access(handle_rec,hwparams_play,SND_PCM_ACCESS_RW_NONINTERLEAVED)) < 0){
    fprintf(stderr, "Unable to set the PCM access type: %s\n",
	    snd_strerror(err));
    snd_pcm_close(handle_play);
    snd_pcm_close(handle_rec);
    return oct_retval;
  }

  printf("|---------------------------------|--------------------\n");
  printf("|         Parameter               | Playback / Capture \n");
  printf("|---------------------------------|--------------------\n");
  
  //
  // Test Double buffering.
  //

  if (snd_pcm_hw_params_is_batch(hwparams_play))
    printf("| Double buffering data transfers | Yes");
  else
    printf("| Double buffering data transfers | No");

  if (snd_pcm_hw_params_is_batch(hwparams_rec))
    printf(" / Yes \n");
  else
    printf(" / No  \n");

  //
  // Test Block transfers.
  //

  if (snd_pcm_hw_params_is_block_transfer(hwparams_play))
    printf("| Block transfers of samples      | Yes");
  else
    printf("| Block transfers of samples      | No");

  if (snd_pcm_hw_params_is_block_transfer(hwparams_rec))
    printf(" / Yes\n");
  else
    printf(" / No \n");

  //
  // Half-duplex.
  //

  if (snd_pcm_hw_params_is_half_duplex(hwparams_play))
    printf("| Half-duplex capable             | Yes");
  else
    printf("| Half-duplex capable             | No");

  if (snd_pcm_hw_params_is_half_duplex(hwparams_rec))
    printf(" / Yes\n");
  else
    printf(" / No\n");

  //      
  // Joint-duplex.
  //

  if (snd_pcm_hw_params_is_joint_duplex(hwparams_play))
    printf("| Joint-duplex capable            | Yes");
  else
    printf("| Joint-duplex capable            | No");

  if (snd_pcm_hw_params_is_joint_duplex(hwparams_rec))
    printf(" / Yes\n");
  else
    printf(" / No\n");

  //
  // Max/min number of channels.
  //

  if ((err=snd_pcm_hw_params_get_channels_max(hwparams_play,&val)) < 0)
    fprintf(stderr,"Unable to get max number of playback channels: %s\n", snd_strerror(err));
  else
    printf("| Max number of channels          | %d",val);
  
  if ((err=snd_pcm_hw_params_get_channels_max(hwparams_play,&val)) < 0)
    fprintf(stderr,"Unable to get max number of capture channels: %s\n", snd_strerror(err));
  else
    printf(" / %d\n",val);


  if ((err=snd_pcm_hw_params_get_channels_min(hwparams_play,&val)) < 0)
    fprintf(stderr,"Unable to get min number of playback channels: %s\n", snd_strerror(err));
  else
    printf("| Min number of channels          | %d",val);

  if ((err=snd_pcm_hw_params_get_channels_min(hwparams_rec,&val)) < 0)
    fprintf(stderr,"Unable to get min number of capture channels: %s\n", snd_strerror(err));
  else
    printf(" / %d\n",val);
  
  //
  // Max/min buffer size.
  //

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_max(hwparams_play,&val2)) < 0)
    fprintf(stderr,"Unable get max buffer size: %s\n", snd_strerror(err));
  else
    printf("| Max buffer size                 | %u",val2);

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_max(hwparams_rec,&val2)) < 0)
    fprintf(stderr,"Unable get max buffer size: %s\n", snd_strerror(err));
  else
    printf(" / %u\n",val2);

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_min(hwparams_play,&val2)) < 0)
    fprintf(stderr,"Unable to get min buffer size: %s\n", snd_strerror(err));
  else
    printf("| Min buffer size                 | %u",val2);

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_min(hwparams_rec,&val2)) < 0)
    fprintf(stderr,"Unable to get min buffer size: %s\n", snd_strerror(err));
  else
    printf(" / %u\n",val2);

  //
  // Max/min buffer size.
  //

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_max(hwparams_play,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get max buffer time: %s\n", snd_strerror(err));
  else
    printf("| Max buffer time                 | %d",val);

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_max(hwparams_rec,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get max buffer time: %s\n", snd_strerror(err));
  else
    printf(" / %d [us]\n",val);

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_min(hwparams_play,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get min buffer time: %s\n", snd_strerror(err));
  else
    printf("| Min buffer time                 | %d",val);

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_min(hwparams_rec,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get min buffer time: %s\n", snd_strerror(err));
  else
    printf(" / %d [us]\n",val);

  //
  // Hardware FIFO size.
  //

  if ((val = snd_pcm_hw_params_get_fifo_size(hwparams_play)) < 0)
    fprintf(stderr,"Unable to get hardware FIFO size: %s\n", snd_strerror(val));
  else
    printf("| Hardware FIFO size              | %d",val);
  
  if ((val = snd_pcm_hw_params_get_fifo_size(hwparams_rec)) < 0)
    fprintf(stderr,"Unable to get hardware FIFO size: %s\n", snd_strerror(val));
  else
    printf(" / %d\n",val);

  //
  // Minimum transfer align value.
  //

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_min_align(hwparams_play,&val2)) < 0) {
    //fprintf(stderr,"Unable to get min align value: %s\n", snd_strerror(err));
    printf("| Min align value                 | na",val2);
  }  else
    printf("| Min align value                 | %u",val2);

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_min_align(hwparams_rec,&val2)) < 0) {
    //fprintf(stderr,"Unable to get min align value: %s\n", snd_strerror(err));
    printf(" / na\n",val2);
  } else
   printf(" / %u\n",val2);


 //
 // Max/min period size.
 //
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_max(hwparams_play,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get max period size: %s\n", snd_strerror(err));
 else
   printf("| Max period size                 | %d",val2);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_max(hwparams_rec,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get max period size: %s\n", snd_strerror(err));
 else
   printf(" /  %d\n",val2);

 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_min(hwparams_play,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get min period size: %s\n", snd_strerror(err));
 else
   printf("| Min period size                 | %d",val2);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_min(hwparams_rec,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get min period size: %s\n", snd_strerror(err));
 else
   printf(" / %d\n",val2);


 //
 // Max/min period time.
 //

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_max(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get max period time: %s\n", snd_strerror(err));
 else
   printf("| Max period time                 | %d",val);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_max(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get max period time: %s\n", snd_strerror(err));
 else
   printf(" / %d [us]\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_min(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get min period time: %s\n", snd_strerror(err));
 else
   printf("| Min period time                 | %d",val);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_min(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get min period time: %s\n", snd_strerror(err));
 else
   printf(" / %d [us]\n",val);

 //
 // Max/min periods.
 //
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_max(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max periods: %s\n", snd_strerror(err));
 else
   printf("| Max periods                     | %d",val);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_max(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max periods: %s\n", snd_strerror(err));
 else
   printf(" / %d\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_min(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min periods: %s\n", snd_strerror(err));
 else
   printf("| Min periods                     | %d",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_min(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min periods: %s\n", snd_strerror(err));
 else
   printf(" / %d\n",val);


 //
 // Max/min rate.
 //

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_rate_max(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max rate: %s\n", snd_strerror(err));
 else
   printf("| Max rate                        | %d",val);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_rate_max(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max rate: %s\n", snd_strerror(err));
 else
   printf(" / %d [Hz]\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_rate_min(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min rate: %s\n", snd_strerror(err));
 else
   printf("| Min rate                        | %d",val);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_rate_min(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min rate: %s\n", snd_strerror(err));
 else
   printf(" / %d [Hz]\n",val);

 //
 // Max/min tick time.
 //

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_tick_time_max(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max tick time: %s\n", snd_strerror(err));
 else
   printf("| Max tick time                   | %d",val);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_tick_time_max(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max tick time: %s\n", snd_strerror(err));
 else
   printf(" / %d [us]\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_tick_time_min(hwparams_play,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min tick time: %s\n", snd_strerror(err));
 else
   printf("| Min tick time                   | %d",val);

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_tick_time_min(hwparams_rec,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min tick time: %s\n", snd_strerror(err));
 else
   printf(" / %d [us]\n",val);

  printf("|---------------------------------|--------------------\n");

  snd_pcm_close(handle_play);
  snd_pcm_close(handle_rec);
  
  return oct_retval;
}
