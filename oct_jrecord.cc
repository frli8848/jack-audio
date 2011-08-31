/***
 *
 * Copyright (C) 2011 Fredrik Lingvall 
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
 * Octave (oct) gateway function for JRECORD.
 *
 ***/

DEFUN_DLD (jrecord, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {} Y = jrecord(frames,jack_ouputs).\n\
\n\
JRECORD Records audio data to the output matrix Y using the (low-latency) audio server JACK.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item frames\n\
A scalar that specifies the number of frames to record/channel.\n\
\n\
@item jack_ouputs\n\
A char matrix with the JACK client output port names, for example, ['system:capture_1'; 'system:capture_2'], etc.\n\
@end table\n\
\n\
@copyright{} 2011 Fredrik Lingvall.\n\
@seealso {jinfo, jplay, @indicateurl{http://jackaudio.org}}\n\
@end deftypefn")
{
  double *Y; 
  int err,verbose = 0;
  octave_idx_type n, frames;
  sighandler_t old_handler, old_handler_abrt, old_handler_keyint;
  char **port_names;
  octave_idx_type buflen;
  octave_idx_type channels;
  
  octave_value_list oct_retval; // Octave return (output) parameters

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  if (nrhs != 2) {
    error("jrecord requires 2 input arguments!");
    return oct_retval;
  }

  if (nlhs > 1) {
    error("Too many output args for jrecord!");
    return oct_retval;
  }

  //
  // Input arg 1 : The number frames/channel.
  //

  const Matrix tmp0 = args(0).matrix_value();

  if ( tmp0.rows() * tmp0.cols() != 1) {
    error("The first input argument must be a scalar!");
    return oct_retval;
  }

  frames = (octave_idx_type) tmp0.fortran_vec()[0];
  if (frames < 0) {
    error("The number of audio frames (rows in arg 1) must > 0!");
    return oct_retval;
  }

  //
  // Input arg 2 : The jack (readable client) ouput audio ports.
  //

  if ( !args(1).is_sq_string() ) {
    error("2rd arg must be a string matrix !");
    return oct_retval;
  }
  
  charMatrix ch = args(1).char_matrix_value();

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
  
  Matrix Ymat(frames, channels);
  Y = Ymat.fortran_vec();

  // Set status to running (CTRL-C will clear the flag and stop capture).
  set_running_flag();

  // Init and connect to the output ports.
  if (record_init(Y, frames, channels, port_names) < 0)
    return oct_retval;

  // Wait until we have recorded all data.
  while(!record_finished() && is_running() ) {
    sleep(1);
  }

  // Cleanup.
  record_close();

  oct_retval.append(Ymat);

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
