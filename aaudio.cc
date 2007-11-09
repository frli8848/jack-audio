#include "aaudio.h"

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

int wait_for_poll_out(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count);
int wait_for_poll_in(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count);

//*********************************************************************************************

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
  int direction, err;
  unsigned int tmp_fs;
  
  snd_pcm_hw_params_alloca(&hwparams);
  
  // Initialize the hw structure.
  if((err = snd_pcm_hw_params_any(handle, hwparams)) < 0){
    fprintf(stderr,"Broken configuration: no configurations available: %s\n",
	    snd_strerror(err));
    return err;
  }
  
  // Set read/write format to MMPAP:ed interleaved .
  if((err = snd_pcm_hw_params_set_access(handle,hwparams,SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0){
    fprintf(stderr, "Unable to set the PCM access type: %s\n",
	    snd_strerror(err));
    return err;
  }
  
  check_hw(hwparams);

  // Test if the audio hardwear supports the chosen audio sample format otherwise try S32,
  // or fallback to S16.
  if(snd_pcm_hw_params_test_format(handle, hwparams,*format) != 0){
    printf("Warning: Unable to set the selected audio format. Trying SND_PCM_FORMAT_S32 instead..."); 
    *format = SND_PCM_FORMAT_S32; // Try S32.
    if(snd_pcm_hw_params_test_format(handle, hwparams,*format) != 0){
      printf("no. Falling back to SND_PCM_FORMAT_S16\n");       
      // Fallback format.
      *format = SND_PCM_FORMAT_S16;
    } else {
      printf("ok.\n"); // SND_PCM_FORMAT_S32 works.      
    }
  }

  // Set the audio sample format.
  if((err = snd_pcm_hw_params_set_format(handle,hwparams,*format)) < 0){
    fprintf(stderr, "Unable to set the sample format: %s\n",
	    snd_strerror(err));
    //exit(-1);
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
    printf("Warning: The number of channels (%d) is outside the min (%d) and max (%d) supported by the device.\n",
	   *channels,min1,max1);
    
    if (*channels > max1)
      *channels = max1;
    
    if (*channels < min1)
      *channels = min1;

    printf("Warning: Trying to use %d channels.\n",*channels);
  }
  
  if((err = snd_pcm_hw_params_set_channels(handle, hwparams,*channels)) < 0) {
    fprintf(stderr, "Warning: Unable to set the number of channels: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }
  
  if (val > max1 || val < min1)
    printf("Warning: Using %d channels.\n",*channels);
    
  /* From aplay.
  unsigned int buffer_time = 0, period_time = 0;
  if ((err = snd_pcm_hw_params_get_buffer_time_max(hwparams,&buffer_time, 0)) < 0) {
    fprintf(stderr, "Cannot get max buffer time: %s\n",
	    snd_strerror(err));
  }
  printf("buffer_time %d\n", buffer_time);
  period_time = buffer_time / 4;

  err = snd_pcm_hw_params_set_period_time_near(handle,hwparams,&period_time, 0);
  */

  //
  // Set approximate target period size in frames (Frames/Period). The chosen approximate target period 
  // size is returned.
  //

  // First get max and min values supporded by the device.
  direction = 0;
  max2 = 0;
  if ((err=snd_pcm_hw_params_get_period_size_max(hwparams,&max2,&direction)) < 0)
    fprintf(stderr,"Unable to get max period size: %s\n", snd_strerror(err));
  
  direction = 0;
  min2 = 0;
  if ((err=snd_pcm_hw_params_get_period_size_min(hwparams,&min2,&direction)) < 0)
    fprintf(stderr,"Unable to get min period size: %s\n", snd_strerror(err));

  if (*period_size > max2 || *period_size < min2)  
    printf("Warning: The period size (%d) is outside the min (%d) and max (%d) supported by the device.\n",
	   *period_size,min2,max2);
  
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
  
  //printf("chunk_size = %d buffer_size = %d\n",chunk_size,*buffer_size);

  //  check_hw(handle,hwparams);
  
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
    printf("Max buffer size = %u\n",val2);

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_min(hwparams,&val2)) < 0)
    fprintf(stderr,"Unable to get min buffer size: %s\n", snd_strerror(err));
  else
    printf("Min buffer size = %u\n",val2);

  //
  // Max/min buffer size.
  //
  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_max(hwparams,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get max buffer time: %s\n", snd_strerror(err));
  else
    printf("Max buffer time = %d [us]\n",val);

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_min(hwparams,&val,&dir)) < 0)
    fprintf(stderr,"Unable to get min buffer time: %s\n", snd_strerror(err));
  else
    printf("Min buffer time = %d [us]\n",val);
  
  // Hardware FIFO size.
  if ((val = snd_pcm_hw_params_get_fifo_size(hwparams)) < 0)
    fprintf(stderr,"Unable to get hardware FIFO size: %s\n", snd_strerror(val));
  else
    printf("Hardware FIFO size = %d\n",val);

 // Minimum transfer align value.
  val2 = 0;
 if ((err=snd_pcm_hw_params_get_min_align(hwparams,&val2)) < 0)
   fprintf(stderr,"Unable to get min align value: %s\n", snd_strerror(err));
 else
   printf("Min align value = %u [samples]\n",val2);

 //
 // Max/min period size.
 //
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_max(hwparams,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get max period size: %s\n", snd_strerror(err));
 else
   printf("Max period size = %d\n",val2);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_min(hwparams,&val2,&dir)) < 0)
   fprintf(stderr,"Unable to get min period size: %s\n", snd_strerror(err));
 else
   printf("Min period size = %d\n",val2);


 //
 // Max/min period time.
 //
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_max(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get max period time: %s\n", snd_strerror(err));
 else
   printf("Max period time = %d [us]\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_min(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Unable to get min period time: %s\n", snd_strerror(err));
 else
   printf("Min period time = %d [us]\n",val);

 //
 // Max/min periods.
 //
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_max(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max periods: %s\n", snd_strerror(err));
 else
   printf("Max periods = %d\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_periods_min(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min periods: %s\n", snd_strerror(err));
 else
   printf("Min periods = %d\n",val);


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
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_tick_time_max(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max tick time: %s\n", snd_strerror(err));
 else
   printf("Max tick time = %d [us]\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_tick_time_min(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min tick time: %s\n", snd_strerror(err));
 else
   printf("Min tick time = %d [us]\n",val);
 
 
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
  
  // Align all transfers to 1 sample.
  err = snd_pcm_sw_params_set_xfer_align(handle, swparams, 1);
  if (err < 0) {
    fprintf(stderr,"Unable to set transfer align for playback: %s\n", snd_strerror(err));
    return err;
  }
  
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


//*********************************************************************************************

/***
 *
 * Write (play) poll loop
 * 
 * 
 *
 * JACK is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU GPL and LGPL licenses as published by 
 * the Free Software Foundation, <http://www.gnu.org> 
 *
 ***/

snd_pcm_sframes_t play_poll_loop(snd_pcm_t *handle,
			    unsigned int nfds, 
			    unsigned int poll_timeout,
			    pollfd *pfd,
			    snd_pcm_uframes_t period_size)
{
  snd_pcm_sframes_t avail = 0;
  snd_pcm_sframes_t play_avail = 0;
  int need_play = 1, err;
  unsigned int tmp_nfds; 
  unsigned int i; // teller. 
  int xrun_true = 0;
  unsigned short revents;


  //if ((err = snd_pcm_prepare(handle)) < 0) {
  //  fprintf(stderr, "Cannot prepare audio device:%s\n",snd_strerror(err));
  //} 

  // Poll-loop
  while (need_play) { 
    
    int p_timeout; 
    
    // finne riktig antall poll descriptors. Dette kan variere med
    // pcm-type
    // ALSA fikser dette. Setter ogs� riktige events. 
    tmp_nfds = 0;
    if (need_play){
      snd_pcm_poll_descriptors(handle,&(pfd[tmp_nfds]),nfds);
      tmp_nfds += nfds;
    }
    


    // Legge til pollevent err. // This is useless according to poll man page.
    //    for (i = 0; i < tmp_nfds; i++)
    //      pfd[i].events |= POLLERR;
    

    //if ((err = snd_pcm_wait(handle, 1000)) < 0) {
    //  fprintf (stderr, "poll failed (%s)\n", snd_strerror (err));
    //  break;
    //}
    
    if (poll (pfd, tmp_nfds,poll_timeout) < 0) {
      // poll error. 
      perror("poll error");
      //return -1;
    }
		
    p_timeout = 0;
    if (need_play) {

      snd_pcm_poll_descriptors_revents(handle,&(pfd[0]),nfds,&revents);
      if(revents & POLLERR){
	xrun_true = 1;
      }


      //printf("nfds=%d, pfd: fd=%d revents=%d tmp_nfds=%d\n",nfds,pfd[0].fd,revents,tmp_nfds);
      
      
      //if (revents & POLLOUT)
      //printf("Got a POLLOUT event!\n");
      
      if(revents == 0) {
	// Timeout. Ingen events. 
	p_timeout++;
      }
    }
    
    if (p_timeout == 0){
      // play har event. Trenger ikke mer poll.
      need_play = 0;
    }
    
    if (p_timeout && (p_timeout == nfds)) {
      fprintf(stderr, "poll timeout.\n");
      //return 0;
    }
    
  } // while (need_play).
  


  if ((play_avail = snd_pcm_avail_update(handle)) < 0) {
    if(play_avail == -EPIPE){
      xrun_true = 1;
    } else {
      fprintf(stderr, "Feil i avail_update(play)\n");
      //return -1;
    }
  }

  if(xrun_true) {
    printf("XRUN\n");
    xrun_recovery(handle,play_avail);
    //printf("XRUN\n");
    //snd_pcm_drop(handle);
    //snd_pcm_prepare(handle);
    //xrun_recovery();
    //return 0;
  }
  
  avail = play_avail;
  
#ifdef DEBUG
  fprintf(stderr, "poll loop, avail = %lu, playavail = %lu\n", MIN(avail, period_size), play_avail);
#endif
  
  return MIN(avail, period_size);
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
			snd_pcm_sframes_t framesize)
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
  //while (1) {
  while((frames - frames_played) > 0) { // Loop until all frames are played.

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

    frames_to_write = snd_pcm_avail_update(handle); 
    if (frames_to_write < 0) {
      err = xrun_recovery(handle, frames_to_write);
      if (err < 0) {
	printf("avail update failed: %s\n", snd_strerror(err));
	//return EXIT_FAILURE;
      }
      first = 1;
      continue;
    }

    if (frames_to_write >  (frames - frames_played) )
      frames_to_write = frames - frames_played; 
    
    if (frames_to_write > 0){
      
      nwritten = 0;
      while(frames_to_write > 0){
	
	// �nsket sammenhengende omr�de. 
	contiguous = frames_to_write; 
	
	if ((err = snd_pcm_mmap_begin(handle,&play_areas,&offset,&frames_to_write)) < 0) {
	  if ((err = xrun_recovery(handle, err)) < 0) {
	    printf("MMAP begin avail error: %s\n", snd_strerror(err));
	    //return EXIT_FAILURE;
	  }
	}
	
	// Test if the number if available frames exceeds the remaining
	// number of frames to write.
	if (contiguous > frames_to_write)
	  contiguous = frames_to_write;
	
	memcpy( (((unsigned char*) play_areas->addr) + offset * framesize),
		(((unsigned char*) buffer) + (frames_played+nwritten) * framesize),
		(contiguous * framesize));
	
	commit_res = snd_pcm_mmap_commit(handle,offset,contiguous);
	if ( (commit_res < 0) || ((snd_pcm_uframes_t) commit_res != contiguous) ) {
	  if ((err = xrun_recovery(handle, commit_res >= 0 ? -EPIPE : commit_res)) < 0) {
	    printf("MMAP commit error: %s\n", snd_strerror(err));
	    //return EXIT_FAILURE;
	  }
	  //first = 1;
	}
	
	if (contiguous > 0) {
	  frames_to_write -= contiguous;
	  nwritten += contiguous;
	} else
	  printf("Warning: Negative byte count\n"); // This should never happend. 

      }
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
			snd_pcm_sframes_t framesize)
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

  frames_recorded = 0;
  init = 1;
  //while (1) {
  while((frames - frames_recorded) > 0) { // Loop until all frames are recorded.

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


    // TODO this should be inside the while loop below,
    // above snd_pcm_mmap_begin, accoring to also documantation.
    // Fix also this for play.
    frames_to_read = snd_pcm_avail_update(handle); 
    if (frames_to_read < 0) {
      err = xrun_recovery(handle, frames_to_read);
      if (err < 0) {
	printf("avail update failed: %s\n", snd_strerror(err));
	//return EXIT_FAILURE;
      }
      //first = 1;
      //continue;
    }

    if (frames_to_read >  (frames - frames_recorded) )
      frames_to_read = frames - frames_recorded; 
    
    if (frames_to_read > 0){
      
      nwritten = 0;
      while(frames_to_read > 0){
	
	// �nsket sammenhengende omr�de. 
	contiguous = frames_to_read; 
	
	if ((err = snd_pcm_mmap_begin(handle,&record_areas,&offset,&frames_to_read)) < 0) {
	  if ((err = xrun_recovery(handle, err)) < 0) {
	    printf("MMAP begin avail error: %s\n", snd_strerror(err));
	    //return EXIT_FAILURE;
	  }
	}
	
	// Test if the number if available frames exceeds the remaining
	// number of frames to read.
	if (contiguous > frames_to_read)
	  contiguous = frames_to_read;
	
	memcpy( (((unsigned char*) buffer) + (frames_recorded+nwritten) * framesize),
		(((unsigned char*) record_areas->addr) + offset * framesize),
		(contiguous * framesize));
	
	commit_res = snd_pcm_mmap_commit(handle,offset,contiguous);
	if ( (commit_res < 0) || ((snd_pcm_uframes_t) commit_res != contiguous) ) {
	  if ((err = xrun_recovery(handle, commit_res >= 0 ? -EPIPE : commit_res)) < 0) {
	    printf("MMAP commit error: %s\n", snd_strerror(err));
	    //return EXIT_FAILURE;
	  }
	  //first = 1;
	}
	
	if (contiguous > 0) {
	  frames_to_read -= contiguous;
	  nwritten += contiguous;
	} else
	  printf("Warning: Negative byte count\n"); // This should never happend. 

      }
    }
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
