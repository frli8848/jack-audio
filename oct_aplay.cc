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
 * Fredrik Lingvall 2007-10-31 : File created.
 * Fredrik Lingvall 2007-11-01 : Added input arg checks.
 * Fredrik Lingvall 2007-11-02 : Added ALSA floating point support.
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

//
// Function prototypes.
//

int xrun_recovery(snd_pcm_t *handle, int err);
int write_loop(snd_pcm_t *handle,
	       adata_type *samples,
	       int channels);
int set_hwparams(snd_pcm_t *handle);
int set_swparams(snd_pcm_t *handle, snd_pcm_uframes_t avail_min,
		 snd_pcm_uframes_t start_threshold,
		 snd_pcm_uframes_t stop_threshold);



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
      //break;	/* skip one period */
    }
    ptr += err * channels;
    cptr -= err;
  }
}

/***
 *
 * Setter hardware-parametre. Ved feil, print feilmelding og avslutt. 
 *
 */

int set_hwparams(snd_pcm_t *handle, driver_t *driver)
{
  snd_pcm_hw_params_t *hwparams;
  snd_pcm_hw_params_alloca(&hwparams);
  
  int direction = 0;
  int err;
  
  if((err = snd_pcm_hw_params_any(handle, hwparams)) < 0){
		fprintf(stderr, "Kan ikke initialisere parameter struktur: %s\n",
			snd_strerror(err));
		exit(-1);
  }

  if((err = snd_pcm_hw_params_set_access(handle, hwparams,
					 SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0){
    fprintf(stderr, "Kan ikke sette Aksesstype: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  // Try to set format1, else set format 2, which should fit. 
  if(snd_pcm_hw_params_test_format(handle, hwparams, 
				   FORMAT1) == 0){
    driver.format = FORMAT1;
  }else{
    driver.format = FORMAT2;
  }

  if((err = snd_pcm_hw_params_set_format(handle, hwparams,
					 driver.format)) < 0){
    fprintf(stderr, "Kan ikke sette sampleformat: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }
  
  if((err = snd_pcm_hw_params_set_rate_near(handle, hwparams,
					    &(driver.rate), &direction)) < 0){
    fprintf(stderr, "Kan ikke sette samplerate: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  if((err = snd_pcm_hw_params_set_channels(handle, hwparams,
					   driver.num_channels)) < 0){
    fprintf(stderr, "Kan ikke sette antall kanaler: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  direction = 0;
  if((err = snd_pcm_hw_params_set_period_size_near(handle, hwparams,
						   &(driver.period_size), &direction)) < 0){
    fprintf(stderr, "Kan ikke sette periodest�rrelse: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  direction = 0;
  if((err = snd_pcm_hw_params_set_periods_near(handle, hwparams,
					       &(driver.num_periods), &direction)) < 0){
    fprintf(stderr, "Kan ikke sette antall perioder: %s\n",
	    snd_strerror(err));
    exit(-1);
  }
	
  if((err = snd_pcm_hw_params(handle, hwparams)) < 0){
    fprintf(stderr, "Kan ikke sette HW parametre: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }
  
  return 0;
}


/***
 *
 * Setter software-parametre for en pcm handle.
 *
 ***/

int set_swparams(snd_pcm_t *handle, snd_pcm_uframes_t avail_min,
		 snd_pcm_uframes_t start_threshold,
		 snd_pcm_uframes_t stop_threshold)
{
  snd_pcm_sw_params_t *swparams;
  snd_pcm_sw_params_alloca(&swparams);
  int err;
  
  if((err = snd_pcm_sw_params_current(handle, swparams)) < 0){
    fprintf(stderr, "Kan ikke initiere SW parameter struktur: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }
 
  if((err = snd_pcm_sw_params_set_avail_min(handle,
					    swparams, avail_min)) < 0){
    fprintf(stderr, "Kan ikke sette grense for minimum avail: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }
  
  if((err = snd_pcm_sw_params_set_start_threshold(handle,
						  swparams, start_threshold)) < 0){
    fprintf(stderr, "Kan ikke sette start-terskel%s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  if((err = snd_pcm_sw_params_set_stop_threshold(handle,
						 swparams, stop_threshold)) < 0){
    fprintf(stderr, "Kan ikke sette stop-terskel%s\n",
	    snd_strerror(err));
    //exit(-1);
  }
  
  if((err = snd_pcm_sw_params(handle, swparams)) < 0){
    fprintf(stderr, "Kan ikke sette SW parametre: %s\n",
	    snd_strerror(err));
    //exit(-1);
  }

  return 0;
}




/***
 * 
 * Octave (oct) gateway function for APLAY.
 *
 ***/

DEFUN_DLD (aplay, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  [Y] = aplay(A).\n\
\n\
APLAY Computes one dimensional convolutions of the columns in the matrix A and the matrix (or vector) B.\n\
\n\
Input parameters:\n\
\n\
@copyright{2007-10-31 Fredrik Lingvall}.\n\
@seealso {play, record}\n\
@end deftypefn")
{
  double *A; 
  int err;
  //unsigned int i,m,n;
  octave_idx_type i,m,n;
  int channels,fs;
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames,oframes;
  driver_t driver; 
  adata_type *buffer;
  char device[50];
  int  buflen;
  //char *device = "plughw:1,0";
  //char *device = "hw:1,0";
  //char *device = "default";
  octave_value_list oct_retval; 

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  if ((nrhs < 1) || (nrhs > 3)) {
    error("aplay requires 1 to 3 input arguments!");
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
      error("Error in 2nd arg. The samping frequency must be > 0!");
      return oct_retval;
    }
  } else
    fs = 8000; // Default to 8 kHz.

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


  // Allocate buffer space.
  buffer = (adata_type*) malloc(frames*channels*sizeof(adata_type));
  
  // Convert to interleaved audio data.
  for (n = 0; n < channels; n++) {
    for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
#ifdef USE_ALSA_FLOAT
      buffer[i] =  (adata_type) CLAMP(A[m], -1.0,1.0);
#else
      buffer[i] =  (adata_type) CLAMP(32768.0*A[m], -32768, 32767);
#endif
    }
  }



  /*
  // Open in blocking mode (0, SND_PCM_NONBLOCK, or SND_PCM_ASYNC).
  if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)) < 0) {
  error("Playback open error: %s\n", snd_strerror(err));
  return oct_retval;
  }
  
  if ((err = snd_pcm_set_params(handle,
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
  snd_pcm_close(handle);
  return oct_retval;
  }
  */


  driver.device_name = device;
  driver.block = 0; // (0, SND_PCM_NONBLOCK, or SND_PCM_ASYNC).
  driver.rate = fs;
  driver.period_size = 2;    // PERIOD_SIZE; ?
  driver.num_periods = 64; //NUM_PERIODS; Number of frames between interupts (=latency?)
  driver.num_channels = channels;
  
  // �pne to PCM-devicer. En for lesing og en for skriving.
  if ((err = snd_pcm_open(&handle,device,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)) < 0) {
    error("Playback open error: %s\n", snd_strerror(err));
    return oct_retval;
  }
  
  set_hwparams(driver.play_handle);
  // swparams: (handle, min_avail, start_thres, stop_thres)
  set_swparams(handle,
	       driver.period_size >> 1,
	       driver.period_size >> 3,
	       driver.period_size << 3);
  
  sample_bytes = snd_pcm_format_width(driver.format)/8;
  framesize = channels * sample_bytes;
  driver.buf = calloc(/*driver.num_periods **/ driver.period_size,
		      driver.framesize);

  nfds = snd_pcm_poll_descriptors_count(handle);
  pfd = malloc(sizeof(struct pollfd));
  poll_timeout = floor(1.5 * 1000000 * 
			      driver.period_size / driver.rate);
#if 1
  // Infoutskrifter. 
  snd_output_t *snderr;
  snd_output_stdio_attach(&snderr ,stderr, 0);
  
  fprintf(stderr, "play_state:%d\n", snd_pcm_state(handle));
  snd_pcm_dump_setup(handle, snderr);
#endif


  //err = write_loop(handle,buffer,frames,channels);
  //if (err < 0)
  //  printf("snd_pcm_writei failed: %s\n", snd_strerror(err));

  /*  
  oframes = snd_pcm_writei(handle, buffer, frames);

  if (oframes < 0)
    oframes = snd_pcm_recover(handle, oframes, 0);
  
  if (oframes < 0)
    printf("snd_pcm_writei failed: %s\n", snd_strerror(err));
  
  if (oframes > 0 && oframes < frames)
    printf("Short write (expected %li, wrote %li)\n", frames, oframes);
  */

  free(driver.pfd);
  free(driver.buf);
  snd_pcm_close(handle);
  free(buffer);
  
  return oct_retval;
  
}
