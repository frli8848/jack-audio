/***
 *
 *  Copyright (C) 2008 by Fredrik Lingvall
 *
 *  Parts of this code is based on the aplay program by Jaroslav Kysela and
 *  the pcm.c example from the alsa-lib.
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


#include "aaudio.h"

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

//
// Macros.
//

#ifdef CLAMP
#undef CLAMP
#endif
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

#ifdef MIN
#undef MIN
#endif
#define MIN(a, b) (a) < (b) ? (a) : (b)

#define TRUE  1
#define FALSE 0

//
// Globals.
//

volatile int running;
volatile int interleaved;

//
// Function prototypes.
//

int wait_for_poll_out(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count);
int wait_for_poll_in(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count);

/***
 *
 * Functions for CTRL-C support.
 *
 */

int is_running(void)
{

  return running;
}


void set_running_flag(void)
{
  running = 1;
  
  return;
}

void clear_running_flag(void)
{
  running = 0;
  
  return;
}


/***
 *
 * Return the data access mathod. 
 *
 *
 ***/

int is_interleaved(void)
{

  return interleaved;
}


/***
 *
 * Setup the hardware parameters.
 *
 */

int set_hwparams(snd_pcm_t *handle,
		 snd_pcm_format_t *format,
		 unsigned int *fs,
		 unsigned int *channels,
		 snd_pcm_uframes_t *period_size,
		 unsigned int *num_periods,
		 snd_pcm_uframes_t *buffer_size)
{
  snd_pcm_hw_params_t *hwparams;
  snd_pcm_format_t tmp_format;
  unsigned int val,max1,min1;
  snd_pcm_uframes_t max2,min2;
  int direction, err, verbose = 0;
  unsigned int tmp_fs;
  
  snd_pcm_hw_params_alloca(&hwparams);
  
  // Initialize the hw structure.
  if((err = snd_pcm_hw_params_any(handle, hwparams)) < 0){
    fprintf(stderr,"Broken configuration: no configurations available: %s\n",
	    snd_strerror(err));
    return err;
  }
  
  //
  // Set read/write format.
  //

  // Try interleaved first (RME cards don't seem to support this).
  interleaved = 1;
  if((err = snd_pcm_hw_params_set_access(handle,hwparams,SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0){
    
    // Interleaved MMAP failed. Try non-interleaved MMAP.
    interleaved = 0;
    if((err = snd_pcm_hw_params_set_access(handle,hwparams,SND_PCM_ACCESS_MMAP_NONINTERLEAVED)) < 0){
      fprintf(stderr, "Unable to set the PCM access type: %s\n",
	      snd_strerror(err));
      return err;
    }
  }
  
  // Test if the audio hardwear supports the chosen audio sample format, otherwise, first try S32
  // and, if that fails too, fallback to S16.
  if(snd_pcm_hw_params_test_format(handle, hwparams,*format) != 0){

    if (verbose)
      printf("Warning: Unable to set the selected audio format. Trying SND_PCM_FORMAT_S32 instead..."); 
    
    *format = SND_PCM_FORMAT_S32; // Try S32.
    if(snd_pcm_hw_params_test_format(handle, hwparams,*format) != 0){
    
      if (verbose)
	printf("no.\n");       
      
      printf("Warning. Unable to set the select audio format, falling back to SND_PCM_FORMAT_S16\n");       
      // Fallback format.
      *format = SND_PCM_FORMAT_S16;
    } else {
      if (verbose)
	printf("ok.\n"); // SND_PCM_FORMAT_S32 works.      
    }
  }

  // Set the audio sample format.
  if((err = snd_pcm_hw_params_set_format(handle,hwparams,*format)) < 0){
    fprintf(stderr, "Unable to set the sample format: %s\n",
	    snd_strerror(err));
  }

  //
  // Set the sampling frequency.
  //

  // First get max and min values supporded by the device.
  direction = 0;
  tmp_fs = *fs;
  if ((err=snd_pcm_hw_params_get_rate_max(hwparams,&max1,&direction)) < 0)
    fprintf(stderr,"Unable to get max rate: %s\n", snd_strerror(err));
  
  if ((err=snd_pcm_hw_params_get_rate_min(hwparams,&min1,&direction)) < 0)
    fprintf(stderr,"Unable to get min rate: %s\n", snd_strerror(err));

  if (*fs > max1 || *fs < min1) {
    printf("Warning: The sampling rate (%d) is outside the min (%d) and max (%d) supported by the device.\n",
	   *fs,min1,max1);

    if (*fs > max1)
      *fs = max1;
    
    if (*fs < min1)
      *fs = min1;

    printf("Warning: Trying to use the  rate %d.\n",*fs);

  }

  direction = 0;
  if((err = snd_pcm_hw_params_set_rate_near(handle, hwparams,fs, &direction)) < 0){
    fprintf(stderr, "Warning: Unable to set the sampling rate: %s\n",snd_strerror(err));
    //return err;
  }

  if (*fs != tmp_fs)
    printf("Warning: Using the sampling rate %d.\n",*fs);
  
  //
  // Set the number of channels.
  //

  // First get max and min values supporded by the device.
  if ((err=snd_pcm_hw_params_get_channels_max(hwparams,&max1)) < 0)
    fprintf(stderr,"Unable to get max number of channels: %s\n", snd_strerror(err));
  
  if ((err=snd_pcm_hw_params_get_channels_min(hwparams,&min1)) < 0)
    fprintf(stderr,"Unable to get min number of channels: %s\n", snd_strerror(err));
  
  val = *channels;
  if (*channels > max1 || *channels < min1) {
    
    if (*channels > max1)
      *channels = max1;
    
    if (*channels < min1)
      *channels = min1;

  }
  
  if((err = snd_pcm_hw_params_set_channels(handle, hwparams,*channels)) < 0) {
    fprintf(stderr, "Warning: Unable to set the number of channels: %s\n",
	    snd_strerror(err));
  }
  
  //
  // Set approximate target period size in frames (Frames/Period). The chosen approximate target period 
  // size is returned.
  //

  // First get max and min values supporded by the device.
  direction = 0;
  max2 = 0;
  if ((err=snd_pcm_hw_params_get_period_size_max(hwparams,&max2,&direction)) < 0) {
    if (verbose)
      fprintf(stderr,"Unable to get max period size: %s\n", snd_strerror(err));
  }

  direction = 0;
  min2 = 0;
  if ((err=snd_pcm_hw_params_get_period_size_min(hwparams,&min2,&direction)) < 0) {
    if (verbose)
      fprintf(stderr,"Unable to get min period size: %s\n", snd_strerror(err));
  }

  if (*period_size > max2 || *period_size < min2) {
    if (verbose)
      printf("Warning: The period size (%d) is outside the min (%d) and max (%d) supported by the device.\n",
	     (int) *period_size, (int) min2, (int) max2);
  }

  direction = 0;
  if((err = snd_pcm_hw_params_set_period_size_near(handle, hwparams,period_size, &direction)) < 0){
    fprintf(stderr, "Unable to set the period size: %s\n",snd_strerror(err));
    return err;
  }

  //  
  // Set approximate number of periods in the buffer (Periods/Buffer). The chosen approximate number of
  // periods per buffer is returned.
  //

  direction = 0;
  if((err = snd_pcm_hw_params_set_periods_near(handle, hwparams,num_periods, &direction)) < 0){
    fprintf(stderr, "Unable to set the number of periods: %s\n",snd_strerror(err));
    return err;
  }
  
  if((err = snd_pcm_hw_params(handle, hwparams)) < 0){
    fprintf(stderr,"Unable to set HW parameters: %s\n",snd_strerror(err));
    return err;
  }

  //snd_pcm_uframes_t chunk_size = 0;
  //snd_pcm_hw_params_get_period_size(hwparams, &chunk_size, 0);
  if((err = snd_pcm_hw_params_get_buffer_size(hwparams, buffer_size)) < 0){
    fprintf(stderr,"Unable to get the buffer size: %s\n",snd_strerror(err));
    return err;
  }
  
  return 0;
}


/***
 *
 * Probe and print out various hardware parameters.
 *
 *
 ***/

void check_hw(snd_pcm_hw_params_t *hwparams)
{
  unsigned int val;
  snd_pcm_uframes_t val2;
  int err,dir;
  
  // Double buffering.
  if (snd_pcm_hw_params_is_batch(hwparams))
    printf("The hardware can do double buffering data transfers for given configuration\n");
  else
    printf("The hardware cannot do double buffering data transfers for given configuration\n");


  // Block transfers.
  if (snd_pcm_hw_params_is_block_transfer(hwparams))
    printf("The hardware can do block transfers of samples for given configuration\n");
  else
    printf("The hardware cannot do block transfers of samples for given configuration\n");

  // Half-duplex.
  if (snd_pcm_hw_params_is_half_duplex(hwparams))
    printf("The hardware does half-duplex\n");
  else
    printf("The hardware doesn't do half-duplex\n");
      
  // Joint-duplex.
  if (snd_pcm_hw_params_is_joint_duplex(hwparams))
    printf("The hardware can do joint-duplex.\n");
  else
    printf("The hardware doesn't do joint-duplex\n");

  //
  // Max/min number of channels.
  //

  if ((err=snd_pcm_hw_params_get_channels_max(hwparams,&val)) < 0)
    fprintf(stderr,"Unable to get max number of channels: %s\n", snd_strerror(err));
  else
    printf("Max number of channels = %d\n",val);

  if ((err=snd_pcm_hw_params_get_channels_min(hwparams,&val)) < 0)
    fprintf(stderr,"Unable to get min number of channels: %s\n", snd_strerror(err));
  else
    printf("Min number of channels = %d\n",val);

  //
  // Max/min buffer size.
  //

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_max(hwparams,&val2)) < 0)
    fprintf(stderr,"Unable get max buffer size: %s\n", snd_strerror(err));
  else
    printf("Max buffer size = %u\n", (unsigned int) val2);

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_min(hwparams,&val2)) < 0)
    fprintf(stderr,"Unable to get min buffer size: %s\n", snd_strerror(err));
  else
    printf("Min buffer size = %u\n", (unsigned int) val2);

  //
  // Max/min buffer size.
  //

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_max(hwparams,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get max buffer time: %s\n", snd_strerror(err));
  else
    printf("Max buffer time = %d [us]\n", val);

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_min(hwparams,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get min buffer time: %s\n", snd_strerror(err));
  else
    printf("Min buffer time = %d [us]\n", val);
  
  // Hardware FIFO size.
  if ((val = snd_pcm_hw_params_get_fifo_size(hwparams)) < 0)
    fprintf(stderr,"Unable to get hardware FIFO size: %s\n", snd_strerror(val));
  else
    printf("Hardware FIFO size = %d\n", val);

 // Minimum transfer align value.
  val2 = 0;
 if ((err=snd_pcm_hw_params_get_min_align(hwparams,&val2)) < 0)
   fprintf(stderr,"Unable to get min align value: %s\n", snd_strerror(err));
 else
   printf("Min align value = %u [samples]\n", (unsigned int) val2);

 //
 // Max/min period size.
 //

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_max(hwparams,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get max period size: %s\n", snd_strerror(err));
 else
   printf("Max period size = %d\n", (int) val2);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_min(hwparams,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get min period size: %s\n", snd_strerror(err));
 else
   printf("Min period size = %d\n",(int) val2);


 //
 // Max/min period time.
 //

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_max(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get max period time: %s\n", snd_strerror(err));
 else
   printf("Max period time = %d [us]\n", val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_min(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get min period time: %s\n", snd_strerror(err));
 else
   printf("Min period time = %d [us]\n", val);

 //
 // Max/min periods.
 //

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_max(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max periods: %s\n", snd_strerror(err));
 else
   printf("Max periods = %d\n", val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_min(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min periods: %s\n", snd_strerror(err));
 else
   printf("Min periods = %d\n", val);


 //
 // Max/min rate.
 //

 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_rate_max(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max rate: %s\n", snd_strerror(err));
 else
   printf("Max rate = %d [Hz]\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_rate_min(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min rate: %s\n", snd_strerror(err));
 else
   printf("Min rate = %d [Hz]\n",val);

 //
 // Max/min tick time.
 //


 // These are deprecated (in pcm.h now).
 
 //dir = 0;
 //val2 = 0;
 //if ((err=snd_pcm_hw_params_get_tick_time_max(hwparams,&val,&dir)) < 0)
 //  fprintf(stderr,"Can't get max tick time: %s\n", snd_strerror(err));
 //else
 //  printf("Max tick time = %d [us]\n",val);
 
 //dir = 0;
 //val2 = 0;
 //if ((err=snd_pcm_hw_params_get_tick_time_min(hwparams,&val,&dir)) < 0)
 //  fprintf(stderr,"Can't get min tick time: %s\n", snd_strerror(err));
 //else
 //  printf("Min tick time = %d [us]\n",val);
 
 
 return;
}

/***
 *
 * Set software parameters for en pcm handle.
 *
 ***/

int set_swparams(snd_pcm_t *handle,
		 snd_pcm_uframes_t avail_min,
		 snd_pcm_uframes_t start_threshold,
		 snd_pcm_uframes_t stop_threshold)
{
  snd_pcm_sw_params_t *swparams;
  snd_pcm_sw_params_alloca(&swparams);
  int err;
  
  if((err = snd_pcm_sw_params_current(handle, swparams)) < 0){
    fprintf(stderr, "Unable to determine current swparams: %s\n",
	    snd_strerror(err));
    return err;
  }
 
  // Set the minimum number of frames between interrupts. 
  //
  //Most PC sound cards can only accept power of 2 frame counts (i.e., 512, 1024, 2048).
  if((err = snd_pcm_sw_params_set_avail_min(handle,swparams,avail_min)) < 0){
    fprintf(stderr, "Unable to set minimum available count: %s\n",
	    snd_strerror(err));
    return err;
  }
  
  // Set the number of frames required before the PCM device is started. 
  //
  // PCM is automatically started when playback frames available to PCM are >= start_threshold.
  if((err = snd_pcm_sw_params_set_start_threshold(handle,swparams,start_threshold)) < 0){
    fprintf(stderr, "Unable to set start threshold mode: %s\n",
	    snd_strerror(err));
    return err;
  }

  // Set the pcm stop threshold (it is perhaps not needed to set this one?)
  //
  // PCM is automatically stopped in SND_PCM_STATE_XRUN state when available frames is >= stop_threshold. 
  // If the stop threshold is equal to boundary (also software parameter - sw_param) then automatic 
  // stop will be disabled (thus device will do the endless loop in the ring buffer).
  if((err = snd_pcm_sw_params_set_stop_threshold(handle,swparams,stop_threshold)) < 0){
    fprintf(stderr, "Cannot set stop mode (threshold) : %s\n",snd_strerror(err));
    return err;
  }

  // This one is deprecated (in pcm.h now).
  //
  // Align all transfers to 1 sample.
  //err = snd_pcm_sw_params_set_xfer_align(handle, swparams, 1);
  //if (err < 0) {
  //  fprintf(stderr,"Unable to set transfer align for playback: %s\n", snd_strerror(err));
  //  return err;
  //}
  
  if((err = snd_pcm_sw_params(handle, swparams)) < 0){
    fprintf(stderr, "Cannot set the software parameters: %s\n",snd_strerror(err));
    return err;
  }

  return 0;
}

/***
 *
 *   Underrun and suspend recovery.
 *
 ***/
 
int xrun_recovery(snd_pcm_t *handle, int err)
{
  if (err == -EPIPE) {	// Under-run.
    err = snd_pcm_prepare(handle);
    if (err < 0)
      printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
    return 0;
  } else if (err == -ESTRPIPE) {

    while ((err = snd_pcm_resume(handle)) == -EAGAIN)
      sleep(1);	// Wait until the suspend flag is released.
    
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
 *   Transfer method - write (play) and wait for room in buffer using poll.
 *
 ***/

int wait_for_poll_out(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count)
{
  unsigned short revents;

  while (1) {
    poll(ufds, count, -1);
    snd_pcm_poll_descriptors_revents(handle, ufds, count, &revents);
    if (revents & POLLERR)
      return -EIO;
    if (revents & POLLOUT)
      return 0;
  }
}

int write_and_poll_loop(snd_pcm_t *handle,
			const snd_pcm_channel_area_t *play_areas,
			snd_pcm_format_t format, 
			void *buffer,
			snd_pcm_sframes_t frames,
			snd_pcm_sframes_t framesize,
			unsigned int channels)
{
  struct pollfd *ufds;
  int err, count, init;
  snd_pcm_uframes_t frames_to_write;
  snd_pcm_sframes_t contiguous; 
  snd_pcm_uframes_t nwritten;
  snd_pcm_uframes_t offset;
  snd_pcm_sframes_t frames_played;
  snd_pcm_sframes_t commit_res;
  int first = 0;
  size_t ch;

  count = snd_pcm_poll_descriptors_count(handle);
  if (count <= 0) {
    printf("Invalid poll descriptors count\n");
    return count;
  }

  ufds = (struct pollfd*) malloc(sizeof(struct pollfd) * count);
  if (ufds == NULL) {
    printf("No enough memory\n");
    return -ENOMEM;
  }

  if ((err = snd_pcm_poll_descriptors(handle, ufds, count)) < 0) {
    printf("Unable to obtain poll descriptors for playback: %s\n", snd_strerror(err));
    return err;
  }

  frames_played = 0;
  init = 1;
  while((frames - frames_played) > 0 && running) { // Loop until all frames are played.

    if (!init) {

      err = wait_for_poll_out(handle, ufds, count);
      if (err < 0) {
	if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN || 
	    snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
	  err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
	  if (xrun_recovery(handle, err) < 0) {
	    printf("Write error: %s\n",snd_strerror(err));
	    return EXIT_FAILURE;
	  }
	  init = 1;
	} else {
	  printf("Wait for poll failed\n");
	  return err;
	}
      }
    }

    // Get the number of available frames.
    frames_to_write = snd_pcm_avail_update(handle); 
    if (frames_to_write < 0) {
      err = xrun_recovery(handle, frames_to_write);
      if (err < 0) {
	printf("avail update failed: %s\n", snd_strerror(err));
	//return EXIT_FAILURE;
      }
    }

    if (frames_to_write >  (frames - frames_played) )
      frames_to_write = frames - frames_played; 
    
    nwritten = 0;
    while (frames_to_write > 0) {

      contiguous = frames_to_write; 
      
      if ((err = snd_pcm_mmap_begin(handle,&play_areas,&offset,&frames_to_write)) < 0) {
	if ((err = xrun_recovery(handle, err)) < 0) {
	  printf("MMAP begin avail error: %s\n", snd_strerror(err));
	  //return EXIT_FAILURE;
	}
      }
      
      // Test if the number of available frames exceeds the remaining
      // number of frames to write.
      if (contiguous > frames_to_write)
	contiguous = frames_to_write;
      
      if (interleaved) {
	memcpy( (((unsigned char*) play_areas->addr) + offset * framesize),
		(((unsigned char*) buffer) + frames_played * framesize),
		(contiguous * framesize));
      } else { // Non-interleaved
	// A separate ring buffer for each channel.
	for (ch=0; ch<channels; ch++) {
	  //printf(" channel %d : %p\n",n, play_areas[ch].addr);
	  memcpy(  (((unsigned char*) play_areas[ch].addr) + offset * framesize/channels),
		   (((unsigned char*) buffer) 
		    + frames_played * framesize/channels 
		    + ch*frames*(framesize/channels)),
	  	   (contiguous * framesize/channels));
	}
      }

      commit_res = snd_pcm_mmap_commit(handle,offset,contiguous);
      if ( (commit_res < 0) || ((snd_pcm_uframes_t) commit_res != contiguous) ) {
	if ((err = xrun_recovery(handle, commit_res >= 0 ? -EPIPE : commit_res)) < 0) {
	  printf("MMAP commit error: %s\n", snd_strerror(err));
	  return EXIT_FAILURE;
	}
      }
      
      if (contiguous > 0) {
	frames_to_write -= contiguous;
	nwritten += contiguous;
	//frames_played += contiguous;
      } else
	printf("Warning: Negative byte count\n"); // This should never happend. 
    }
    frames_played += nwritten;
    
    if (snd_pcm_state(handle) < 3) {
      snd_pcm_start(handle);
    }
    
    // It is possible, that the initial buffer cannot store
    // all data from the last period, so wait awhile.
    err = wait_for_poll_out(handle, ufds, count);
      if (err < 0) {
	if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN ||
	    snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
	  err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
	  if (xrun_recovery(handle, err) < 0) {
	    printf("Write error: %s\n", snd_strerror(err));
	    //return EXIT_FAILURE;
	  }
	  init = 1;
	} else {
	  printf("Wait for poll failed\n");
	  return err;
	}
      }
  } // while((frames - frames_played) > 0)
  
  free(ufds);
  
  return 0;
}

/***
 *
 *   Transfer method - read (record) and wait for room in buffer using poll.
 *
 ***/

int wait_for_poll_in(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count)
{
  unsigned short revents;

  while (1) {
    poll(ufds, count, -1);
    snd_pcm_poll_descriptors_revents(handle, ufds, count, &revents);
    if (revents & POLLERR)
      return -EIO;
    if (revents & POLLIN)
      return 0;
  }
}

int read_and_poll_loop(snd_pcm_t *handle,
		       const snd_pcm_channel_area_t *record_areas,
		       snd_pcm_format_t format, 
		       void *buffer,
		       snd_pcm_sframes_t frames,
		       snd_pcm_sframes_t framesize,
		       unsigned int channels)
{
  struct pollfd *ufds;
  int err, count, init;
  snd_pcm_uframes_t frames_to_read;
  snd_pcm_sframes_t contiguous; 
  snd_pcm_uframes_t nwritten;
  snd_pcm_uframes_t offset;
  snd_pcm_sframes_t frames_recorded;
  snd_pcm_sframes_t commit_res;
  int first = 0;
  size_t ch;

  count = snd_pcm_poll_descriptors_count(handle);
  if (count <= 0) {
    printf("Invalid poll descriptors count\n");
    return count;
  }

  ufds = (struct pollfd*) malloc(sizeof(struct pollfd) * count);
  if (ufds == NULL) {
    printf("No enough memory\n");
    return -ENOMEM;
  }

  if ((err = snd_pcm_poll_descriptors(handle, ufds, count)) < 0) {
    printf("Unable to obtain poll descriptors for capture: %s\n", snd_strerror(err));
    return err;
  }

  //
  // Main read loop.
  //

  frames_recorded = 0;
  init = 1;
  while((frames - frames_recorded) > 0 && running) { // Loop until all frames are recorded.

    if (!init) {

      err = wait_for_poll_in(handle, ufds, count);
      if (err < 0) {
	if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN || 
	    snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
	  err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
	  if (xrun_recovery(handle, err) < 0) {
	    printf("Read error: %s\n",snd_strerror(err));
	    return EXIT_FAILURE;
	  }
	  init = 1;
	} else {
	  printf("Wait for poll failed\n");
	  return err;
	}
      }
    }

    // Get the number of available frames.
    frames_to_read = snd_pcm_avail_update(handle); 
    if (frames_to_read < 0) {
      err = xrun_recovery(handle, frames_to_read);
      if (err < 0) {
	printf("avail update failed: %s\n", snd_strerror(err));
	//return EXIT_FAILURE;
      }
    }

    if (frames_to_read >  (frames - frames_recorded) )
      frames_to_read = frames - frames_recorded; 
    
    nwritten = 0;
    while(frames_to_read > 0){
      
      contiguous = frames_to_read; 
      
      if ((err = snd_pcm_mmap_begin(handle,&record_areas,&offset,&frames_to_read)) < 0) {
	if ((err = xrun_recovery(handle, err)) < 0) {
	  printf("MMAP begin avail error: %s\n", snd_strerror(err));
	  //return EXIT_FAILURE;
	}
      }
      
      // Test if the number of available frames exceeds the remaining
      // number of frames to read.
      if (contiguous > frames_to_read)
	contiguous = frames_to_read;
      
      if (interleaved) {
	memcpy( (((unsigned char*) buffer) + frames_recorded * framesize),
	      (((unsigned char*) record_areas->addr) + offset * framesize),
	      (contiguous * framesize));
      } else { // Non-interleaved
	// A separate ring buffer for each channel.
	for (ch=0; ch<channels; ch++) {
	  memcpy( (((unsigned char*) buffer) 
		   + frames_recorded * framesize/channels 
		   + ch*frames*(framesize/channels)),
		  (((unsigned char*) record_areas[ch].addr) + offset * framesize/channels),
		  (contiguous * framesize/channels));
	}
      }
      
      commit_res = snd_pcm_mmap_commit(handle,offset,contiguous);
      if ( (commit_res < 0) || ((snd_pcm_uframes_t) commit_res != contiguous) ) {
	if ((err = xrun_recovery(handle, commit_res >= 0 ? -EPIPE : commit_res)) < 0) {
	  printf("MMAP commit error: %s\n", snd_strerror(err));
	  return EXIT_FAILURE;
	}
      }
      
      if (contiguous >= 0) {
	frames_to_read -= contiguous;
	nwritten += contiguous;
	//frames_recorded += contiguous;
      } else
	printf("Warning: Zero or negative byte count\n"); // This should never happend. 
    
    } // while (frames_to_read > 0)
    frames_recorded += nwritten;
    
    if (snd_pcm_state(handle) < 3) {
      snd_pcm_start(handle);
    }
    
    // It is possible, that the initial buffer cannot store
    // all data from the last period, so wait awhile.
    err = wait_for_poll_in(handle, ufds, count);
    if (err < 0) {
      if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN ||
	  snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
	err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
	if (xrun_recovery(handle, err) < 0) {
	  printf("Read error: %s\n", snd_strerror(err));
	  //return EXIT_FAILURE;
	}
	init = 1;
      } else {
	printf("Wait for poll failed\n");
	return err;
      }
    }
    
    
  } // while((frames - frames_recorded) > 0)
  
  free(ufds);
  
  return 0;
}

/***
 *
 * read_and_poll_loop_ringbuffer
 *
 * Function that continously read the audio stream and 
 * saves that data in a ring buffer when the input signal
 * is over the trigger level.
 *
 *
 * Returns the position of the last aquired frame in 
 * the ring buffer.
 *
 ***/

snd_pcm_sframes_t 
t_read_and_poll_loop(snd_pcm_t *handle,
		     const snd_pcm_channel_area_t *record_areas,
		     snd_pcm_format_t format, 
		     void *buffer,
		     snd_pcm_sframes_t frames,
		     snd_pcm_sframes_t framesize,
		     unsigned int channels,
		     double trigger_level,
		     int trigger_ch,
		     snd_pcm_sframes_t trigger_frames)
{
  struct pollfd *ufds;
  int err, count, init;
  snd_pcm_uframes_t frames_to_read;
  snd_pcm_sframes_t contiguous; 
  snd_pcm_uframes_t nwritten;
  snd_pcm_uframes_t offset;
  snd_pcm_sframes_t frames_recorded, post_trigger_frames = 0;
  snd_pcm_sframes_t commit_res;
  int first = 0;
  size_t n, n2, ch, trigger_position = 0;
  double *triggerbuffer = NULL, trigger = 0;
  int trigger_active = FALSE, has_wrapped = FALSE;
  float *fbuffer = NULL;
  int *ibuffer = NULL;
  short *sbuffer = NULL;

  ch = (int) trigger_ch;

  // Flag used to stop the data aquisition.
  int ringbuffer_read_running = TRUE;
  
  // The current position in the ring buffer.
  snd_pcm_sframes_t ringbuffer_position = 0;

  // Allocate space and clear the trigger buffer.
  triggerbuffer = (double*) malloc(trigger_frames*framesize*sizeof(double));
  bzero(triggerbuffer, trigger_frames*framesize*sizeof(double));

  ringbuffer_read_running = TRUE;
  ringbuffer_position = 0;

  count = snd_pcm_poll_descriptors_count(handle);
  if (count <= 0) {
    printf("Invalid poll descriptors count\n");
    return count;
  }

  ufds = (struct pollfd*) malloc(sizeof(struct pollfd) * count);
  if (ufds == NULL) {
    printf("Not enough memory\n");
    return -ENOMEM;
  }

  if ((err = snd_pcm_poll_descriptors(handle, ufds, count)) < 0) {
    printf("Unable to obtain poll descriptors for capture: %s\n", snd_strerror(err));
    return err;
  }

  //
  // Main read loop.
  //

  printf("\n Audio capturing started. Listening to channel 1 for a trigger signal.\n\n");

  frames_recorded = 0;
  init = 1;
  while(running &&  ringbuffer_read_running) { // Loop until reading flag is cleared (or CTRL-C).
    
    if (!init) {

      err = wait_for_poll_in(handle, ufds, count);
      if (err < 0) {
	if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN || 
	    snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
	  err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
	  if (xrun_recovery(handle, err) < 0) {
	    printf("Read error: %s\n",snd_strerror(err));
	    return EXIT_FAILURE;
	  }
	  init = 1;
	} else {
	  printf("Wait for poll failed\n");
	  return err;
	}
      }
    }

    // Get the number of available frames.
    frames_to_read = snd_pcm_avail_update(handle); 
    if (frames_to_read < 0) {
      err = xrun_recovery(handle, frames_to_read);
      if (err < 0) {
	printf("avail update failed: %s\n", snd_strerror(err));
	//return EXIT_FAILURE;
      }
    }

    if (frames_to_read >  (frames - frames_recorded) )
      frames_to_read = frames - frames_recorded; 
    
    nwritten = 0;
    while(frames_to_read > 0){
      
      contiguous = frames_to_read; 
      
      if ((err = snd_pcm_mmap_begin(handle,&record_areas,&offset,&frames_to_read)) < 0) {
	if ((err = xrun_recovery(handle, err)) < 0) {
	  printf("MMAP begin avail error: %s\n", snd_strerror(err));
	  //return EXIT_FAILURE;
	}
      }
      
      // Test if the number of available frames exceeds the remaining
      // number of frames to read.
      if (contiguous > frames_to_read)
      	contiguous = frames_to_read;
      
      if (interleaved) {
	
	memcpy( (((unsigned char*) buffer) + frames_recorded * framesize),
		(((unsigned char*) record_areas->addr) + offset * framesize),
		(contiguous * framesize));
	
      } else { // Non-interleaved
	
	// A separate ring buffer for each channel.
	for (ch=0; ch<channels; ch++) {
	  memcpy( (((unsigned char*) buffer) 
		   + frames_recorded * framesize/channels 
		   + ch*frames*(framesize/channels)),
		  (((unsigned char*) record_areas[ch].addr) + offset * framesize/channels),
		  (contiguous * framesize/channels));
	}
      }
     
      commit_res = snd_pcm_mmap_commit(handle,offset,contiguous);
      if ( (commit_res < 0) || ((snd_pcm_uframes_t) commit_res != contiguous) ) {
	if ((err = xrun_recovery(handle, commit_res >= 0 ? -EPIPE : commit_res)) < 0) {
	  printf("MMAP commit error: %s\n", snd_strerror(err));
	  return EXIT_FAILURE;
	}
      }

      // 
      // Update the trigger buffer with the new audio data och check 
      // if we are above the trigger threshold.
      //

      // TODO: This will probably not work if the number new frames (=contiguous)
      // is larger than the size of the trigger buffer.

      if (!trigger_active) {

	// 1) "Forget" the old data which is now shifted out of the trigger buffer.
	// We have got 'contiguous' new frames so forget the 'contiguous' oldest ones. 
	
	for (n=0; n<contiguous; n++) {

	  n2 = (trigger_position + n) % trigger_frames;
	  
	  trigger -= fabs(triggerbuffer[n2]);
	}
	
	// 2) Add the new data to the trigger ring buffer.

	switch(format) {

	case SND_PCM_FORMAT_FLOAT:
	  fbuffer = (float*) (((unsigned char*) buffer) + frames_recorded * framesize);
	  // Copy and convert data to doubles.
	  for (n=0; n<contiguous; n++) {
	    
	    n2 = (trigger_position + n) % trigger_frames;

	    if (interleaved) 
	      triggerbuffer[n2] = (double) fbuffer[(n+ch)*channels];  
	    else  // Non-interleaved.
	      triggerbuffer[n2] = (double) fbuffer[ch*frames + n];
	    
	  }
	  break;    

	case SND_PCM_FORMAT_S32:
	  ibuffer = (int*) (((unsigned char*) buffer) + frames_recorded * framesize);
	  // Copy, convert to doubles, and normalize data.
	  n2 = 0;
	  for (n=0; n<contiguous; n++) {
	    
	    n2 = (trigger_position + n) % trigger_frames;

	    if (interleaved) 
	      triggerbuffer[n2] = ((double) ibuffer[(n+ch)*channels]) / 2147483648.0; // Normalize audio data.  
	    else  // Non-interleaved.
	      triggerbuffer[n2] = ((double) ibuffer[ch*frames + n]) / 2147483648.0; // Normalize audio data.

	  }
	  break;

	case SND_PCM_FORMAT_S16:
	  sbuffer = (short*) (((unsigned char*) buffer) + frames_recorded * framesize);
	  // Copy, convert to doubles, and normalize data.
	  for (n=0; n<contiguous; n++) {

	    n2 = (trigger_position + n) % trigger_frames;

	    if (interleaved) 
	      triggerbuffer[n2] = ((double) sbuffer[(n+ch)*channels]) / 32768.0; // Normalize audio data.
	    else  // Non-interleaved.
	      triggerbuffer[n2] = ((double) sbuffer[ch*frames + n]) / 32768.0; // Normalize audio data.
	  }
	  break;
	  
	default: // SND_PCM_FORMAT_S16 
	  sbuffer = (short*) (((unsigned char*) buffer) + frames_recorded * framesize);
	  // Copy, convert to doubles, and normalize data.
	  for (n=0; n<contiguous; n++) {
	    
	    n2 = (trigger_position + n) % trigger_frames;

	    if (interleaved) 
	      triggerbuffer[n2] = ((double) sbuffer[(n+ch)*channels]) / 32768.0; // Normalize audio data.
	    else  // Non-interleaved.
	      triggerbuffer[n2] = ((double) sbuffer[ch*frames + n]) / 32768.0; // Normalize audio data.

	  }
	}

	// 3) Update the trigger value.
	
	for (n=0; n<contiguous; n++) {

	  n2 = (trigger_position + n) % trigger_frames;

	  trigger += fabs(triggerbuffer[n2]);
	}

	// 4) Set the new position in the trigger buffer.

	trigger_position = n2;	

	// Check if we are above the threshold.
	if ( (trigger / (double) trigger_frames) > trigger_level) {
	  trigger_active = TRUE;
	  printf("\n Got a trigger signal!\n\n");
	}
	
      } else { // We have already detected a signal just wait until we have got all the requested data. 
	post_trigger_frames += contiguous; // Add the number of aquired frames.
      }

      if (contiguous >= 0) {
	frames_to_read -= contiguous;
	nwritten += contiguous;
	//frames_recorded += contiguous;
      } else
	printf("Warning: Zero or negative byte count\n"); // This should never happend. 

      // The current position of the ring buffer.
      ringbuffer_position += contiguous;
    
    } // while (frames_to_read > 0)
    frames_recorded += nwritten;
    
    if (snd_pcm_state(handle) < 3) {
      snd_pcm_start(handle);
    }
    
    // It is possible, that the initial buffer cannot store
    // all data from the last period, so wait awhile.
    err = wait_for_poll_in(handle, ufds, count);
    if (err < 0) {
      if (snd_pcm_state(handle) == SND_PCM_STATE_XRUN ||
	  snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
	err = snd_pcm_state(handle) == SND_PCM_STATE_XRUN ? -EPIPE : -ESTRPIPE;
	if (xrun_recovery(handle, err) < 0) {
	  printf("Read error: %s\n", snd_strerror(err));
	  //return EXIT_FAILURE;
	}
	init = 1;
      } else {
	printf("Wait for poll failed\n");
	return err;
      }
    }

    // If we have reached the end of the ring buffer then 
    // start from the beginning. 
    if ( (frames - frames_recorded) == 0) { // TODO: Have we missed data if frames_recorded > frames?
	frames_recorded = 0;
	ringbuffer_position = 0;
	has_wrapped = TRUE; // To indicated that the ring buffer has been full.
    }

    // We have got a trigger. Now wait for frames/2 more data and then
    // we're done aquiring data.
    if (trigger_active && (post_trigger_frames >= frames/2) )
      ringbuffer_read_running = FALSE; // Exit the read loop.
    
  } // while(running && ringbuffer_read_running) 

  // Note that the data is not sequential in time in the buffer, that is,
  // the buffer must be shifted (if wrapped)  by the calling function/program.

  free(triggerbuffer);
  free(ufds);

  // If the ring buffer never has been wrapped (been full) then we should not shift it.
  if (!has_wrapped) 
    ringbuffer_position = 0;

  return ringbuffer_position;
}

/***
 *
 * List audio devices and PCMs.
 *
 *
 * Code from the alsa-utils application aplay (by Jaroslav Kysel and Michael Beck).
 *
 ***/


void device_list(int play_or_rec)
{
  snd_ctl_t *handle;
  int card, err, dev, idx;
  snd_ctl_card_info_t *info;
  snd_pcm_info_t *pcminfo;
  snd_pcm_stream_t stream;
  snd_ctl_card_info_alloca(&info);
  snd_pcm_info_alloca(&pcminfo);


  if (play_or_rec > 0)
    stream = SND_PCM_STREAM_PLAYBACK;
  else
    stream = SND_PCM_STREAM_CAPTURE;

  card = -1;
  if (snd_card_next(&card) < 0 || card < 0) {
    error("no soundcards found...");
    return;
  }
  printf("**** List of %s Hardware Devices ****\n",snd_pcm_stream_name(stream));
  while (card >= 0) {
    char name[32];
    //printf("ALSA device: hw:%d\n", card);
    sprintf(name, "hw:%d", card);
    if ((err = snd_ctl_open(&handle, name, 0)) < 0) {
      error("control open (%i): %s", card, snd_strerror(err));
      goto next_card;
    }
    if ((err = snd_ctl_card_info(handle, info)) < 0) {
      error("control hardware info (%i): %s", card, snd_strerror(err));
      snd_ctl_close(handle);
      goto next_card;
    }
    dev = -1;
    while (1) {
      unsigned int count;
      if (snd_ctl_pcm_next_device(handle, &dev)<0)
	error("snd_ctl_pcm_next_device");
      if (dev < 0)
	break;
      snd_pcm_info_set_device(pcminfo, dev);
      snd_pcm_info_set_subdevice(pcminfo, 0);
      snd_pcm_info_set_stream(pcminfo, stream);
      if ((err = snd_ctl_pcm_info(handle, pcminfo)) < 0) {
	if (err != -ENOENT)
	  error("control digital audio info (%i): %s", card, snd_strerror(err));
	continue;
      }
      printf("card %i: %s [%s], device %i: %s [%s]\n",
	     card, snd_ctl_card_info_get_id(info), snd_ctl_card_info_get_name(info),
	     dev,
	     snd_pcm_info_get_id(pcminfo),
	     snd_pcm_info_get_name(pcminfo));
      count = snd_pcm_info_get_subdevices_count(pcminfo);
      printf("  Subdevices: %i/%i\n",
	      snd_pcm_info_get_subdevices_avail(pcminfo), count);
      for (idx = 0; idx < (int)count; idx++) {
	snd_pcm_info_set_subdevice(pcminfo, idx);
	if ((err = snd_ctl_pcm_info(handle, pcminfo)) < 0) {
	  error("control digital audio playback info (%i): %s", card, snd_strerror(err));
	} else {
	  printf("  Subdevice #%i: %s\n",
		 idx, snd_pcm_info_get_subdevice_name(pcminfo));
	}
      }
    }
    snd_ctl_close(handle);
  next_card:
    if (snd_card_next(&card) < 0) {
      error("snd_card_next");
      break;
    }
  }
}

void pcm_list(void)
{
  void **hints, **n;
  char *name, *descr, *descr1, *io;
  const char *filter;
  snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;

  if (snd_device_name_hint(-1, "pcm", &hints) < 0)
    return;
  n = hints;
  filter = stream == SND_PCM_STREAM_CAPTURE ? "Input" : "Output";
  while (*n != NULL) {
    name = snd_device_name_get_hint(*n, "NAME");
    descr = snd_device_name_get_hint(*n, "DESC");
    io = snd_device_name_get_hint(*n, "IOID");
    if (io != NULL && strcmp(io, filter) == 0)
      goto __end;
    printf("%s\n", name);
    if ((descr1 = descr) != NULL) {
      printf("    ");
      while (*descr1) {
	if (*descr1 == '\n')
	  printf("\n    ");
	else
	  putchar(*descr1);
	descr1++;
      }
      putchar('\n');
    }
  __end:
    if (name != NULL)
      free(name);
    if (descr != NULL)
      free(descr);
    if (io != NULL)
      free(io);
    n++;
  }
  snd_device_name_free_hint(hints);
}
