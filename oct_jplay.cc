/***
 *
 * Copyright (C) 2009,2011 Fredrik Lingvall 
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
 * Octave (oct) gateway function for JPLAY.
 *
 ***/

DEFUN_DLD (jplay, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {} jplay(A,jack_inputs).\n\
\n\
JPLAY Plays audio data from the input matrix A using the (low-latency) audio server JACK.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item A\n\
A frames x number of playback channels matrix.\n\
\n\
@item jack_inputs\n\
A char matrix with the JACK client input port names, for example, ['system:playback_1'; 'system:playback_2'], etc.\n\
@end table\n\
\n\
@copyright{} 2009,2011 Fredrik Lingvall.\n\
@seealso {jinfo, jrecord, @indicateurl{http://jackaudio.org}}\n\
@end deftypefn")
{
  double *A; 
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
    error("jplay requires 2 input arguments!");
    return oct_retval;
  }

  if (nlhs > 0) {
    error("jplay don't have output arguments!");
    return oct_retval;
  }

  //
  // Input arg 1 : The audio data (a frames x channels matrix).
  //

  const Matrix tmp0 = args(0).matrix_value();
  frames   = tmp0.rows();	// Audio data length for each channel.
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
  // Input arg 2 : The jack (writable client) input audio ports.
  //
  

  //if (!mxIsChar(1)) {
  if ( !args(1).is_sq_string() ) {
    error("2rd arg must be a string matrix !");
    return oct_retval;
  }
  
  charMatrix ch = args(1).char_matrix_value();

  if ( ch.rows() != channels ) {
    error("The number of channels to play don't match the specified number of jack client input ports!");
    return oct_retval;
  }

  //std::string strin = args(1).string_value(); 
  //octave_stdout << strin << endl;
  //buflen = strin.length();

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

  // Set status to running (CTRL-C will clear the flag and stop playback).
  set_running_flag();

  // Init and connect to the output ports.
  if (play_init(A, frames, channels, port_names) < 0)
    return oct_retval;

  // Wait until we have played all data.
  while(!play_finished() && is_running() ) {
    sleep(1);
  }

  //
  // Cleanup.
  //
  
  play_close();
  
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
    error("CTRL-C pressed - playback interrupted!\n"); // Bail out.

  return oct_retval;
}
