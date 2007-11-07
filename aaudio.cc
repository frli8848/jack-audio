
#include "aaudio.h"

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
  unsigned int val;
  int direction, err;
  unsigned int tmp_fs;
  
  snd_pcm_hw_params_alloca(&hwparams);
  
  // Initialize the hw structure.
  if((err = snd_pcm_hw_params_any(handle, hwparams)) < 0){
    fprintf(stderr,"Broken configuration: no configurations available: %s\n",
	    snd_strerror(err));
    return(err);
  }
  
  // Set read/write format to MMPAP:ed interleaved .
  if((err = snd_pcm_hw_params_set_access(handle,hwparams,SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0){
    fprintf(stderr, "Cannot set the PCM access type: %s\n",
	    snd_strerror(err));
    return(err);
  }
  
  check_hw(hwparams);

  // Test if the audio hardwear supports the chosen audio sample format otherwise try S32,
  // or fallback to S16.
  if(snd_pcm_hw_params_test_format(handle, hwparams,*format) != 0){
    printf("Warning: Cannot set the selected audio format. Trying SND_PCM_FORMAT_S32 instead\n"); 
    *format = SND_PCM_FORMAT_S32; // Try S32.
    if(snd_pcm_hw_params_test_format(handle, hwparams,*format) != 0){
      printf("Warning: Cannot set the audio format to SND_PCM_FORMAT_S32. Falling back to SND_PCM_FORMAT_S16\n");       
      // Fallback format.
      *format = SND_PCM_FORMAT_S16;
    }
  }

  // Set the audio sample format.
  if((err = snd_pcm_hw_params_set_format(handle,hwparams,*format)) < 0){
    fprintf(stderr, "Kan ikke sette sampleformat: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  // Set the sampling frequency.
  direction = 0;
  tmp_fs = *fs;
  if((err = snd_pcm_hw_params_set_rate_near(handle, hwparams,&tmp_fs, &direction)) < 0){
    fprintf(stderr, "Waring: Cannot set the sampling rate: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  if((err = snd_pcm_hw_params_get_rate(hwparams,&tmp_fs, &direction)) < 0){
    fprintf(stderr, "Waring: Cannot get the sample rate: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }
  if (*fs != tmp_fs)
    printf("Warning using the sampling rate %d instead\n",tmp_fs);

  *fs = tmp_fs;

  // Set the number of channels.
  if((err = snd_pcm_hw_params_set_channels(handle, hwparams,*channels)) < 0) {
    fprintf(stderr, "Warning: Cannot set the number of channels: %s\n",
	    snd_strerror(err));
    
    // Get the max and min number of channels for the device..
    if ((err=snd_pcm_hw_params_get_channels_max(hwparams,&val)) < 0)
      fprintf(stderr,"Can't get max number of channels: %s\n", snd_strerror(err));
    else
      printf("(max number of channels is %d)\n",val);
    
    if ((err=snd_pcm_hw_params_get_channels_min(hwparams,&val)) < 0)
      fprintf(stderr,"Can't get min number of channels: %s\n", snd_strerror(err));
    else
      printf("(min number of channels is %d\n)",val);

    if((err = snd_pcm_hw_params_get_channels(hwparams,channels)) < 0) {
      fprintf(stderr, "Cannot get the number of channels: %s\n",
	      snd_strerror(err));
    }
    fprintf(stderr, "Using channels %d number of channels instead.\n",*channels);
    //exit(-1);
  }

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

  // Set approximate target period size in frames (Frames/Period). The chosen approximate target period 
  // size is returned.
  direction = 0;
  if((err = snd_pcm_hw_params_set_period_size_near(handle, hwparams,period_size, &direction)) < 0){
    fprintf(stderr, "Kan ikke sette periodestørrelse: %s\n",snd_strerror(err));
    //exit(-1);
  }

  // Set approximate number of periods in the buffer (Periods/Buffer). The chosen approximate number of
  // periods per buffer is returned.
  direction = 0;
  if((err = snd_pcm_hw_params_set_periods_near(handle, hwparams,num_periods, &direction)) < 0){
    fprintf(stderr, "Kan ikke sette antall perioder: %s\n",snd_strerror(err));
    //exit(-1);
  }
  
  if((err = snd_pcm_hw_params(handle, hwparams)) < 0){
    fprintf(stderr,"Kan ikke sette HW parametre: %s\n",snd_strerror(err));
    //exit(-1);
  }

  //snd_pcm_uframes_t chunk_size = 0;
  //snd_pcm_hw_params_get_period_size(hwparams, &chunk_size, 0);
  if((err = snd_pcm_hw_params_get_buffer_size(hwparams, buffer_size)) < 0){
    fprintf(stderr, "Cannot get the buffer size: %s\n",snd_strerror(err));
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
    fprintf(stderr,"Can't get max number of channels: %s\n", snd_strerror(err));
  else
    printf("Max number of channels = %d\n",val);

  if ((err=snd_pcm_hw_params_get_channels_min(hwparams,&val)) < 0)
    fprintf(stderr,"Can't get min number of channels: %s\n", snd_strerror(err));
  else
    printf("Min number of channels = %d\n",val);

  //
  // Max/min buffer size.
  //
  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_max(hwparams,&val2)) < 0)
    fprintf(stderr,"Can't get max buffer size: %s\n", snd_strerror(err));
  else
    printf("Max buffer size = %u\n",val2);

  val2 = 0;
  if ((err=snd_pcm_hw_params_get_buffer_size_min(hwparams,&val2)) < 0)
    fprintf(stderr,"Can't get min buffer size: %s\n", snd_strerror(err));
  else
    printf("Min buffer size = %u\n",val2);

  //
  // Max/min buffer size.
  //
  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_max(hwparams,&val,&dir)) < 0)
    fprintf(stderr,"Can't get max buffer time: %s\n", snd_strerror(err));
  else
    printf("Max buffer time = %d [us]\n",val);

  dir = 0;
  val = 0;
  if ((err=snd_pcm_hw_params_get_buffer_time_min(hwparams,&val,&dir)) < 0)
    fprintf(stderr,"Can't get min buffer time: %s\n", snd_strerror(err));
  else
    printf("Min buffer time = %d [us]\n",val);
  
  // Hardware FIFO size.
  if ((val = snd_pcm_hw_params_get_fifo_size(hwparams)) < 0)
    fprintf(stderr,"Can't get hardware FIFO size: %s\n", snd_strerror(val));
  else
    printf("Hardware FIFO size = %d\n",val);

 // Minimum transfer align value.
  val2 = 0;
 if ((err=snd_pcm_hw_params_get_min_align(hwparams,&val2)) < 0)
   fprintf(stderr,"Can't get min align value: %s\n", snd_strerror(err));
 else
   printf("Min align value = %u [samples]\n",val2);

 //
 // Max/min period size.
 //
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_max(hwparams,&val2,&dir)) < 0)
   fprintf(stderr,"Can't get max period size: %s\n", snd_strerror(err));
 else
   printf("Max period size = %d\n",val2);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_size_min(hwparams,&val2,&dir)) < 0)
   fprintf(stderr,"Can't get min period size: %s\n", snd_strerror(err));
 else
   printf("Min period size = %d\n",val2);


 //
 // Max/min period time.
 //
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_max(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get max period time: %s\n", snd_strerror(err));
 else
   printf("Max period time = %d [us]\n",val);
 
 dir = 0;
 val2 = 0;
 if ((err=snd_pcm_hw_params_get_period_time_min(hwparams,&val,&dir)) < 0)
   fprintf(stderr,"Can't get min period time: %s\n", snd_strerror(err));
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


