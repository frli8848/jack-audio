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
#include <signal.h>

#include <iostream>
#include <chrono>
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

void* smp_process(void *arg);
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
  playrec_clear_running_flag();
}

void sig_abrt_handler(int signum) {
  //printf("Caught signal SIGABRT.\n");
}

void sig_keyint_handler(int signum) {
  //printf("Caught signal SIGINT.\n");
}

/***
 *
 * Octave (oct) gateway function for JPLAYREC.
 *
 ***/

DEFUN_DLD (jplayrec, args, nlhs,
           "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {} Y = jplayrec(A,jack_inputs,jack_ouputs,num_skip_buffers);\n\
\n\
JPLAYREC Plays audio data from the input matrix A, on the jack ports given by jack_inputs and \n\
records audio data, from the jack ports given by jack_ouputs, to the output matrix Y using the \n\
(low-latency) audio server JACK.\n\
\n\
Input parameters:\n\
\n\
@table @samp\n\
@item A\n\
A frames x number of playback channels (jack ports) matrix.\n\
@item jack_inputs\n\
A char matrix with the JACK client input port names, for example, ['system:playback_1'; 'system:playback_2'], etc.\n\
@item jack_ouputs\n\
A char matrix with the JACK client output port names, for example, ['system:capture_1'; 'system:capture_2'], etc.\n\
@item num_skip_buffers\n\
The number of JACK periods (buffers) to skip before saving audio data (optional).\n\
@end table\n\
\n\
Output argument:\n\
\n\
@table @samp\n\
@item Y\n\
A frames x channels single precision matrix containing the recorded audio data.\n\
@end table\n\
\n\
@copyright{} 2011,2023 Fredrik Lingvall.\n\
@seealso {jinfo, jplay, jrecord, @indicateurl{http://jackaudio.org}}\n\
@end deftypefn")
{
  double *dA = nullptr;
  float  *fA = nullptr;
  float  *Y = nullptr;
  size_t frames = 0;
  sighandler_t old_handler, old_handler_abrt, old_handler_keyint;
  char **port_names_in = nullptr, **port_names_out = nullptr;
  size_t buflen;
  size_t play_channels = 0, rec_channels = 0;
  int format = FLOAT_AUDIO;

  octave_value_list oct_retval; // Octave return (output) parameters

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  if ( (nrhs < 3) || (nrhs > 4) ) {
    error("jplayrec requires 3 or 4 input arguments!");
  }

  if (nlhs > 1) {
    error("Too many output args for jplayrec!");
  }

  //
  // Input arg 1 : The audio data to play (a frames x channels matrix).
  //

  // Double precision input data.
  if(args(0).is_double_type()) {

    format = DOUBLE_AUDIO;

    const Matrix tmp0 = args(0).matrix_value();
    frames = (size_t) tmp0.rows();		// Audio data length for each channel.
    play_channels = (size_t) tmp0.cols();	// Number of channels.

    dA = (double*) tmp0.data();
  }

  // Single precision input data.
  if(args(0).is_single_type()) {

    format = FLOAT_AUDIO;

    const FloatMatrix tmp0 = args(0).float_matrix_value();
    frames = (size_t) tmp0.rows();		// Audio data length for each channel.
    play_channels = (size_t) tmp0.cols();	// Number of channels.

    fA = (float*) tmp0.data();
  }

  if (frames < 0) {
    error("The number of audio frames (rows in arg 1) must > 0!");
    return oct_retval;
  }

  if (play_channels < 0) {
    error("The number of playback channels (columns in arg 1) must > 0!");
    return oct_retval;
  }

  //
  // Input arg 2 : The jack (writable client) input audio ports.
  //

  if ( !args(1).is_sq_string() ) {
    error("2rd arg must be a string matrix !");
    return oct_retval;
  }

  charMatrix ch_in = args(1).char_matrix_value();

  if ( size_t(ch_in.rows()) != play_channels ) {
    error("The number of channels to play don't match the specified number of jack client input ports!");
  }

  buflen = (size_t) ch_in.cols();
  port_names_in = (char**) malloc(play_channels * sizeof(char*));
  for ( size_t n=0; n<play_channels; n++ ) {

    port_names_in[n] = (char*) malloc(buflen*sizeof(char)+1);

    std::string strin = ch_in.row_as_string(n);

    for (size_t k=0; k<=buflen; k++ )
      if (strin[k] != ' ')  // Cut off the string if its a whitespace char.
        port_names_in[n][k] = strin[k];
      else {
        port_names_in[n][k] = '\0';
        break;
      }

    port_names_in[0][buflen] = '\0';
  }

  //
  // Input arg 3 : The jack (readable client) ouput audio ports.
  //

  if ( !args(2).is_sq_string() ) {
    error("2rd arg must be a string matrix !");
    return oct_retval;
  }

  charMatrix ch_out = args(2).char_matrix_value();

  rec_channels = ch_out.rows();

  buflen = ch_out.cols();
  port_names_out = (char**) malloc(rec_channels * sizeof(char*));
  for ( size_t n=0; n<rec_channels; n++ ) {

    port_names_out[n] = (char*) malloc(buflen*sizeof(char)+1);

    std::string strin = ch_out.row_as_string(n);

    for (size_t k=0; k<=buflen; k++ )
      if (strin[k] != ' ')  // Cut off the string if its a whitespace char.
        port_names_out[n][k] = strin[k];
      else {
        port_names_out[n][k] = '\0';
        break;
      }

    port_names_out[0][buflen] = '\0';
  }

  //
  // Input arg 4 : Number of JACK periods to skip on record
  //

  size_t num_skip_buffers = 0;
  if ( nrhs ==  4) {
    // Must ba a scalar
    const Matrix tmp3 = args(3).matrix_value();
    if (tmp3.rows() * tmp3.cols() != 1 ) {
      error("4:th arg must be a scalar !");
    }

    num_skip_buffers = (size_t) tmp3.data()[0];
  } else {
    num_skip_buffers = 0;
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

  // Allocate memory for the output arg.
  FloatMatrix Ymat( (octave_idx_type) frames, rec_channels);
  Y = (float*) Ymat.data();

  // Set status to running (CTRL-C will clear the flag and stop play/capture).
  playrec_set_running_flag();

  if (format == DOUBLE_AUDIO) {

    const Matrix tmp0 = args(0).matrix_value();
    dA = (double*) tmp0.data();

    // Init playback and connect to the jack input ports.
    if (playrec_init(dA, DOUBLE_AUDIO,
                     play_channels, port_names_out,
                     Y, rec_channels, port_names_in,
                     frames,
                     "octave:jplayrec",
                     num_skip_buffers) < 0) {
      error("jplayrec init failed!");
    }
  }

  if (format == FLOAT_AUDIO) {

    const FloatMatrix tmp0 = args(0).float_matrix_value();
    fA = (float*) tmp0.data();

    // Init playback and record and connect to the jack input ports.

    if (playrec_init(fA, FLOAT_AUDIO,
                     play_channels, port_names_out,
                     Y, rec_channels, port_names_in,
                     frames,
                     "octave:jplayrec",
                     num_skip_buffers) < 0) {

      error("jplayrec init failed!");
    }
  }

  // Wait for both playback and record to finish.
  while( !playrec_finished() && playrec_is_running() ) {
    std::this_thread::sleep_for (std::chrono::milliseconds(50));
  }

  // Close all jack ports and the client.
  playrec_close(play_channels, port_names_out,
                rec_channels, port_names_in);

  // Append the output data.
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

  //if (!playrec_is_running()) {
  //  error("CTRL-C pressed - play and record interrupted!\n"); // Bail out.
  //}

  return oct_retval;
}
