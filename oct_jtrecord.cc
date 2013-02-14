/***
 *
 * Copyright (C) 2011,2012 Fredrik Lingvall 
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

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>

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

#include "jaudio.h"

#define TRUE 1
#define FALSE 0

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
// Globals.
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
 * Octave (oct) gateway function for JTRECORD.
 *
 ***/

DEFUN_DLD (jtrecord, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {} Y = jtrecord(trigger_pars,frames,jack_ouputs).\n\
\n\
JTRECORD Records audio data to the output matrix Y using the (low-latency) audio server JACK.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item trigger_pars\n\
The trigger parameter vector: trigger_pars = [trigger_level,trigger_ch,trigger_frames post_trigger_frames];\n\
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
@table @samp\n\
@item frames\n\
A scalar that specifies the number of frames to record/channel.\n\
\n\
@item jack_ouputs\n\
A char matrix with the JACK client output port names, for example, ['system:capture_1'; 'system:capture_2'], etc.\n\
@end table\n\
@item indicator_file\n\
An optional file name (text string) which, when specified, jtrecord writes a 1 to when a trigger occurs.\n\
@end table\n\
\n\
@copyright{} 2011,2012 Fredrik Lingvall.\n\
@seealso {jinfo, jplay, jrecord, @indicateurl{http://jackaudio.org}}\n\
@end deftypefn")
{
  float *Y; 
  int err,verbose = 0;
  octave_idx_type n, frames;
  sighandler_t old_handler, old_handler_abrt, old_handler_keyint;
  char **port_names;
  octave_idx_type buflen;
  octave_idx_type channels;
  double trigger_level;
  octave_idx_type trigger_ch, trigger_frames, post_trigger_frames;
  char indicator_file[100];
  int write_to_i_file = FALSE;
  
  octave_value_list oct_retval; // Octave return (output) parameters

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  if ( (nrhs != 3) && (nrhs != 4)) {
    error("jtrecord requires 3 or 4input arguments!");
    return oct_retval;
  }

  if (nlhs > 1) {
    error("Too many output args for jtrecord!");
    return oct_retval;
  }

  //
  // Input arg 2 : The number frames/channel.
  //

  const Matrix tmp1 = args(1).matrix_value();

  if ( tmp1.rows() * tmp1.cols() != 1) {
    error("The first input argument must be a scalar!");
    return oct_retval;
  }

  frames = (octave_idx_type) tmp1.fortran_vec()[0];
  if (frames < 0) {
    error("The number of audio frames (rows in arg 1) must > 0!");
    return oct_retval;
  }

  //
  // Input arg 3 : The jack (readable client) ouput audio ports.
  //

  if ( !args(2).is_sq_string() ) {
    error("3rd arg must be a string matrix !");
    return oct_retval;
  }
  
  charMatrix ch = args(2).char_matrix_value();

  channels = ch.rows();

  buflen = ch.cols();    
  port_names = (char**) malloc(channels * sizeof(char*));
  for ( n=0; n<channels; n++ ) {

    port_names[n] = (char*) malloc(buflen*sizeof(char)+1);

    std::string strin = ch.row_as_string(n);
    
    for (int k=0; k<=buflen; k++ )
      if (strin[k] != ' ')  // Cut off the string if its a whitespace char.
	port_names[n][k] = strin[k];
      else {
	port_names[n][k] = '\0';
	break;
      }
    
    port_names[0][buflen] = '\0';
  }

  if (nrhs == 4) {

    if (!mxIsChar(3)) {
      error("Argument 4 must be a string");
      return oct_retval;
    }

    std::string strin = args(3).string_value(); 
    buflen = strin.length();
    for ( n=0; n<=buflen; n++ ) {
      indicator_file[n] = strin[n];
    }
    indicator_file[buflen] = '\0';
    write_to_i_file = TRUE;
  }


  //
  // Input arg 1 : The trigger parameters.
  //

  // Note we must know the sampling freq. to set the default
  // trigger buffer length.
  
  // Check that arg 1 is a 3 element vector
  if (!((mxGetM(0)<=4 && mxGetN(0)==1) || (mxGetM(0)==1 && mxGetN(0)<=4))) {
    error("Argument 1 must be a 1 to 4 element vector!");
    return oct_retval;
  }
  const Matrix tmp0 = args(0).matrix_value();
  double* t_par = (double*) tmp0.fortran_vec();

  if (trigger_level < 0.0 || trigger_level > 1.0) {
    error("Error in 1st arg! The trigger level must be >= 0 and <= 1.0!");
    return oct_retval;
  }
   
  if ( mxGetM(0)*mxGetN(0) >= 2) {

    trigger_ch     = ((octave_idx_type) t_par[1]) - 1; // Trigger channel (1--channels).
    
    if (trigger_ch < 0 || trigger_ch > channels-1) {
      error("Error in arg 1! The trigger channel must be >= 1 and <= %d!",channels);
      return oct_retval;
    }
  } else
    trigger_ch = 0; // Default to a 1st channel.

  if ( mxGetM(0)*mxGetN(0) >= 3) {

    trigger_level  = t_par[0]; // The trigger level (should be between 0.0 and 1.0).
    
    if (trigger_level < 0 || trigger_level > 1.0) {
      error("Error in arg 1! The trigger level must be >= 0 and <= 1.0!");
      return oct_retval;
    }
  } else
    trigger_level = 0.5; // Default to 0.5.

  if ( mxGetM(0)*mxGetN(0) >= 3) {

    trigger_frames = (octave_idx_type) t_par[2]; // The length of the trigger buffer.
    
    if (trigger_frames < 1 || trigger_frames >= frames) {
      error("Error in arg 1! The trigger_frames must be >= 1 and <= %d!",frames);
      return oct_retval;
    }
  } else
    trigger_frames = 1; // Default to a single frame.
  
  if ( mxGetM(0)*mxGetN(0) >= 4) {

    post_trigger_frames = (octave_idx_type) t_par[3]; // The number of frames to record
                                                      // after triggering.
    
    if (post_trigger_frames < 0 || post_trigger_frames > frames) {
      error("Error in arg 1! The post_trigger_frames must be >= 0 and <= %d!", frames);
      return oct_retval;
    }
  } else
    post_trigger_frames = 0; // Default to save immediately.


  //
  // Register signal handlers.
  //

  if ((old_handler = signal(SIGTERM, &sighandler)) == SIG_ERR) {
    error("Couldn't register signal handler.\n");
  }

  if ((old_handler_abrt = signal(SIGABRT, &sighandler)) == SIG_ERR) {
    error("Couldn't register signal handler.\n");
  }
  
  if ((old_handler_keyint = signal(SIGINT, &sighandler)) == SIG_ERR) {
    error("Couldn't register signal handler.\n");
  }

  // 
  // Allocate memory for the output arg.
  //
  
  FloatMatrix Ymat(frames, channels); // Single precision matrix.
  Y = Ymat.fortran_vec();

  // Set status to running (CTRL-C will clear the flag and stop capture).
  set_running_flag(); 

  // Init and connect to the output ports.
  if (t_record_init(Y, frames, channels, port_names, "octave:jtrecord",
		    trigger_level,
		    trigger_ch,
		    trigger_frames,
		    post_trigger_frames) < 0)
    return oct_retval;
  
  // Wait until we have recorded all data.
  while(!t_record_finished() && is_running() ) {
    sleep(1); // Note: This will give delay of 1 sec but it takes some time 
	      // to save data so we will always loose some data if we, for
              // example, call jtrecord in a loop. To fix this we probably need a
              // double buffer approach where we switch to a second buffer while
              // we save data from the first buffer.

    // Write a 1 to an indicator file to indicate that we got a trigger.
    if (got_a_trigger() && write_to_i_file) {
      FILE *fid;
      char one = '1';
      fid = fopen(indicator_file, "w");
      fputc(one,fid);
      fclose(fid);
    }
    
  }

    
  if (is_running()) { // Only do this if we have not pressed CTRL-C.
    
    // Get the position in the ring buffer so we know if we need to unwrap the
    // data.
    octave_idx_type ringbuffer_position = get_ringbuffer_position();
    
    // If the ringbuffer has wrapped then we need to reorder data.
    if (ringbuffer_position > 0) {
      
      octave_stdout << "shifting ring buffer " << " : pos = " << ringbuffer_position << "\n";
      
      float *tmp_data;
      tmp_data = (float*) malloc(ringbuffer_position*1*sizeof(float));
      
      // Shift one channel each time.
      for (n=0; n<channels; n++) {
	
	memcpy(tmp_data, &Y[0 + n*frames], ringbuffer_position*1*sizeof(float));
	
	memmove(&Y[0 + n*frames], &Y[ringbuffer_position + n*frames],
		(frames - ringbuffer_position)*1*sizeof(float));
	
	memcpy(&Y[(frames - ringbuffer_position) + n*frames], tmp_data,
	       ringbuffer_position*1*sizeof(float));
      }
      free(tmp_data);
    }

    oct_retval.append(Ymat);
  }

  //
  // Cleanup.
  //

  // Close the JACK connections and cleanup.
  t_record_close();

  for ( n=0; n<channels; n++ ) {
    if (port_names[n])
      free(port_names[n]);
  }
  
  if (port_names)
    free(port_names);
  
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
    error("CTRL-C pressed - record interrupted!\n"); // Bail out.

  return oct_retval;
}
