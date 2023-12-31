/***
 *
 * Copyright (C) 2011,2012,2023 Fredrik Lingvall
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

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>

#include <iostream>
#include <thread>

#include <octave/oct.h>

#include "jaudio.h"

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
  record_clear_running_flag();
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
@deftypefn {Loadable Function} {} Y = jrecord(frames, jack_inputs).\n\
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
A char matrix with the JACK client input port names, for example, ['system:capture_1'; 'system:capture_2'], etc.\n\
@end table\n\
\n\
Output argument:\n\
\n\
@table @samp\n\
@item Y\n\
A frames x channels single precision matrix containing the recorded audio data.\n\
@end table\n\
\n\
@copyright{} 2011-2023 Fredrik Lingvall.\n\
@seealso {jinfo, jplay,jplayrec, @indicateurl{http://jackaudio.org}}\n\
@end deftypefn")
{
  float *Y;
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

  frames = (octave_idx_type) tmp0.data()[0];
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

  FloatMatrix Ymat(frames, channels);
  Y = (float*) Ymat.data();

  // Set status to running (CTRL-C will clear the flag and stop capture).
  record_set_running_flag();

  // Init and connect to the output ports.
  if (record_init(Y, frames, channels, port_names, "octave:jrecord") < 0) {
    return oct_retval;
  }

  // Wait until we have recorded all data.
  while(!record_finished() && record_is_running() ) {
    std::this_thread::sleep_for (std::chrono::milliseconds(50));
  }

  if (record_is_running()) {
    // Append the output matrix.
    oct_retval.append(Ymat);
  }

  //
  // Cleanup.
  //

  record_close();

  for ( n=0; n<channels; n++ ) {
    if (port_names[n]) {
      free(port_names[n]);
    }
  }

  if (port_names) {
    free(port_names);
  }

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

  if (!record_is_running()) {
    error("CTRL-C pressed - record interrupted!\n"); // Bail out.
  }

  return oct_retval;
}
