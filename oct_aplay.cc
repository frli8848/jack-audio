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
#define USE_ALSA_FLOAT

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


//
// Function prototypes.
//


snd_pcm_sframes_t poll_loop(snd_pcm_t *handle,
			    unsigned int nfds, 
			    unsigned int poll_timeout,
			    pollfd *pfd,
			    snd_pcm_uframes_t period_size);



/***
 *
 * Mainloop som holder styr p� flere fildeskriptorer via en poll().
 * Denne funksjonen er hentet fra prosjektet JACK (www.jackaudio.org),
 * og tilpasset bruk her. 
 *
 * JACK is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU GPL and LGPL licenses as published by 
 * the Free Software Foundation, <http://www.gnu.org> 
 *
 ***/

snd_pcm_sframes_t poll_loop(snd_pcm_t *handle,
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
  snd_pcm_t *handle;
  snd_pcm_sframes_t frames,frames_played;
  unsigned int framesize;
  unsigned int sample_bytes;
  float *fbuffer;
  int *ibuffer;
  short *sbuffer;
  char device[50];
  int  buflen;
  unsigned int nfds;
  unsigned int poll_timeout;
  pollfd *pfd;

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
  
  snd_pcm_uframes_t frames_to_write;
  snd_pcm_sframes_t contiguous; 
  snd_pcm_uframes_t nwritten;
  snd_pcm_uframes_t offset; 
  const snd_pcm_channel_area_t *play_areas;



  octave_value_list oct_retval; 

  int nrhs = args.length ();



  // Set the ALSA audio data format.
  //#ifdef USE_ALSA_FLOAT
  //  format = SND_PCM_FORMAT_FLOAT;
  //#else
  //  format = SND_PCM_FORMAT_S16;
  //#endif

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
  wanted_channels = channels;

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

  //
  // HW/SW parameters
  //
  
  if (nrhs > 3) {    
    
    if (mxGetM(3)*mxGetN(3) != 5) {
      error("4th arg must be a 5 element vector !");
      return oct_retval;
    }
    
    const Matrix tmp3 = args(3).matrix_value();
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

  // Open the PCM playback device. 
  if ((err = snd_pcm_open(&handle,device,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)) < 0) {
    error("Playback open error: %s\n", snd_strerror(err));
    return oct_retval;
  }

  // Setup the hardwear parameters for the playback device.
  if (nrhs <= 3) {
    period_size = 512;
    num_periods = 1;
    //period_size = 16;
    //num_periods = 2;
  }
  format = SND_PCM_FORMAT_FLOAT; // Try to use floating point format.
  set_hwparams(handle,&format,&fs,&channels,&period_size,&num_periods,&buffer_size);

  // If the number of wanted_channels (given by input data) < channels (which depends on hardwear)
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

  // Convert to interleaved audio data.
  for (n = 0; n < channels; n++) {
    for (i = n,m = n*frames; m < (n+1)*frames; i+=channels,m++) {// n:th channel.
      
      switch(format) {
	
      case SND_PCM_FORMAT_FLOAT:
	fbuffer[i] =  (float) CLAMP(A[m], -1.0,1.0);
	break;    
	
      case SND_PCM_FORMAT_S32:
	ibuffer[i] =  (int) CLAMP(-214748364.0*A[m], -2147483648.0, 2147483647.0);
	break;
	
      case SND_PCM_FORMAT_S16:
	sbuffer[i] =  (short) CLAMP(32768.0*A[m], -32768.0, 32767.0);
	break;
	
      default:
	sbuffer[i] =  (short) CLAMP(32768.0*A[m], -32768.0, 32767.0);
      }
    }
  }
  
  printf("fs = %d period_size = %d num_periods = %d\n",fs,period_size,num_periods);

  if (nrhs <= 3) {
    // swparams: (handle, min_avail, start_thres, stop_thres)
    //avail_min = 512; // Play this many frames before interrupt.
    //start_threshold = 0;
    //stop_threshold = 1024;
    //avail_min = period_size/4; 
    //avail_min = 8; 
    avail_min = period_size; // aplay uses this setting. 
    //start_threshold = avail_min/4;
    //start_threshold = 0;
    start_threshold = (buffer_size/avail_min) * avail_min;

    //start_threshold = avail_min;
    stop_threshold = 16*period_size;
    set_swparams(handle,avail_min,start_threshold,stop_threshold);
  }
  sample_bytes = snd_pcm_format_width(format)/8; // Compute the number of bytes per sample.

  // Check if the hardware are using less then 32 bits.
  if ((format == SND_PCM_FORMAT_S32) && (snd_pcm_format_width(format) != 32))
    sample_bytes = 32/8; // Use int to store, for example, data for 24 bit cards. 
  
  framesize = channels * sample_bytes; // Compute the framesize;
  //driver.buf = calloc(period_size,framesize); // Should be frames*channels here.
  
  nfds = snd_pcm_poll_descriptors_count(handle);
  pfd = (pollfd*)  malloc(sizeof(pollfd));
  poll_timeout = (unsigned int) floor(1.5 * 1000000 * period_size / fs);
  //poll_timeout = -1; // Infinite timeout.

#if 1
  // Infoutskrifter. 
  snd_output_t *snderr;
  snd_output_stdio_attach(&snderr ,stderr, 0);
  
  fprintf(stderr, "play_state:%d\n", snd_pcm_state(handle));
  snd_pcm_dump_setup(handle, snderr);
#endif

  //
  // Write the audio data to the PCM device.
  //

  //err = snd_pcm_prepare(handle);
  //snd_pcm_start(handle);   	

  frames_played = 0;
  while((frames - frames_played) > 0) { // Loop until all frames are played.
    
    // Poll the playback device.
    if ((frames_to_write = poll_loop(handle,nfds,poll_timeout,pfd,period_size)) < 0) {
      fprintf(stderr, "Poll loop error\n");
      //return NULL;
    }

    if (frames_to_write >  (frames - frames_played) )
      frames_to_write = frames - frames_played; 

    //if ((err = snd_pcm_writei(handle,fbuffer,frames_to_write) ) < 0) {
    //  fprintf(stderr, "Write error (%s)\n",snd_strerror(err));
    //} else
    //  nwritten = frames_to_write;


    //printf("frames_to_write=%d\n",frames_to_write);
    // Write  
    if (frames_to_write > 0){
      
      nwritten = 0;
      while(frames_to_write > 0){
	
	// �nsket sammenhengende omr�de. 
	contiguous = frames_to_write; 
	
	if ((err = snd_pcm_mmap_begin(handle,&play_areas,&offset,&frames_to_write)) < 0) {
	  fprintf(stderr, "MMAP begin\n");
	  //return -1;
	}

	//printf("frames_to_write=%d\n",frames_to_write);

	if (contiguous > frames_to_write)
	  contiguous = frames_to_write;
	
	switch(format) {
	  
	case SND_PCM_FORMAT_FLOAT:
	  memcpy( (((unsigned char*) play_areas->addr) + offset * framesize),
		  (((unsigned char*) fbuffer) + (frames_played+nwritten) * framesize),
		  (contiguous * framesize));
	  break;    
	  
	case SND_PCM_FORMAT_S32:
	  memcpy( (((unsigned char*) play_areas->addr) + offset * framesize),
		  (((unsigned char*) ibuffer) + (frames_played+nwritten) * framesize),
		  (contiguous * framesize));
	  break;
	  
	case SND_PCM_FORMAT_S16:
	  memcpy( (((unsigned char*) play_areas->addr) + offset * framesize),
		  (((unsigned char*) sbuffer) + (frames_played+nwritten) * framesize),
		  (contiguous * framesize));
	  break;
	  
	default:
	  memcpy( (((unsigned char*) play_areas->addr) + offset * framesize),
		  (((unsigned char*) sbuffer) + (frames_played+nwritten) * framesize),
		  (contiguous * framesize));
	  
	}
	
	if((err = snd_pcm_mmap_commit(handle,offset,contiguous)) < 0){
	  fprintf(stderr, "MMAP commit error\n");
	  //return -1;
	}

	//printf("frames_to_write=%d offset=%d contiguous=%d\n",frames_to_write,offset,contiguous);
	//printf("Remainig frames = %d frames_played = %d\n",frames - frames_played,frames_played );
	if (contiguous > 0) {
	  frames_to_write -= contiguous;
	  nwritten += contiguous;
	} else
	  printf("negative\n");


      }
    }

    //printf("Remainig frames = %d frames_played = %d\n",frames - frames_played,frames_played );
    frames_played += nwritten;
    
    if (snd_pcm_state(handle) < 3) {
      snd_pcm_start(handle);
    }
    
  }




  //
  // Cleanup.
  //

  free(pfd);
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
