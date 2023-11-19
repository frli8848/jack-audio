/***
 *
 * Copyright (C) 2009,2011,2023 Fredrik Lingvall
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

// This code is based on: http://dis-dot-dat.net/index.cgi?item=jacktuts/starting/

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>

#include <iostream>

#include <octave/oct.h>

//#include "jaudio.h"
#include <jack/jack.h>

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

jack_port_t *input_port;
jack_port_t *output_port;

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
  //clear_running_flag();
}

void sig_abrt_handler(int signum) {
  //printf("Caught signal SIGABRT.\n");
}

void sig_keyint_handler(int signum) {
  //printf("Caught signal SIGINT.\n");
}

//
// Jack callback functions.
//

int process_jinfo (jack_nframes_t nframes, void *arg)
{
  jack_default_audio_sample_t *out =
    (jack_default_audio_sample_t *)
    jack_port_get_buffer (output_port, nframes);

  /*
  jack_default_audio_sample_t *in =
    (jack_default_audio_sample_t *)
    jack_port_get_buffer (input_port, nframes);

  // Just copy input to output.
  memcpy (out, in, sizeof (jack_default_audio_sample_t) * nframes);
  */

  // Just fill with silence.
  bzero(out, sizeof (jack_default_audio_sample_t) * nframes);

  return 0;
}

// This is called whenever the sample rate changes.
int srate(jack_nframes_t nframes, void *arg)
{
  printf("The sample rate is now %lu/sec.\n", (long unsigned int) nframes);
  return 0;
}

void jerror(const char *desc)
{
  std::cerr << "JACK error: '" << desc << "'" << std::endl;
  return;
}


void jack_shutdown(void *arg)
{
  //clear_running_flag(); // Stop if JACK shuts down..
  return;
}

void print_jack_status(jack_status_t status)
{

  std::cerr << "JACK status: " << status << std::endl;
}


/***
 *
 * Octave (oct) gateway function for JINFO.
 *
 ***/

DEFUN_DLD (jinfo, args, nlhs,
           "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  [Fs_hz] = jinfo()\n\
\n\
JINFO Prints the input and output ports connected to the\n\
 (low-latency) JACK audio engine.\n\
\n\
Output argument:\n\
\n\
@table @samp\n\
@item Fs_hz\n\
The sampling frequency in Hz (optional).\n\
@end table\n\
\n\
@copyright{} 2023 Fredrik Lingvall.\n\
@seealso {jplay, jrecord, jtrecord, @indicateurl{http://jackaudio.org}}\n\
@end deftypefn")
{
  jack_client_t *client;
  const char **ports_i = nullptr, **ports_o = nullptr;
  jack_port_t  *port = nullptr;
  int  n = 0, port_flags = 0;
  long unsigned int sample_rate = 0;
  octave_value_list oct_retval; // Octave return (output) parameters

  int nrhs = args.length ();

  // Check for proper inputs arguments.

  if (nrhs > 0) {
    error("jinfo don't have any input argument!");
  }

  if (nlhs > 1) {
    error("Too many output args for jinfo!");
  }

  //
  // List all devices if no input arg is given.
  //

  // Try to become a client of the JACK server.
  const char *client_name = "octave:jinfo";
  const char *server_name = NULL;
  jack_options_t options = JackNullOption;
  jack_status_t status;

  client = jack_client_open (client_name, options, &status, server_name);
  if (client == NULL) {
    print_jack_status(status);
    error("jack server not running?\n");

    return oct_retval;
  }

  if (status & JackServerStarted) {
    fprintf (stderr, "JACK server started\n");
  }

  if (status & JackNameNotUnique) {
    client_name = jack_get_client_name(client);
    fprintf (stderr, "unique name `%s' assigned\n", client_name);
  }

  // Tell the JACK server to call `process()' whenever
  // there is work to be done.
  jack_set_process_callback (client, process_jinfo, 0);

  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown (client, jack_shutdown, 0);

  // Tell the JACK server to call error() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.
  //
  // This is set here so that it can catch errors in the
  // connection process.
  //jack_set_error_function (jerror);

  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  //jack_set_sample_rate_callback (client, srate, 0);

  octave_stdout << "|------------------------------------------------------\n";

  // Display the current sample rate. Once the client is activated
  // (see below), you should rely on your own sample rate
  // callback (see above) for this value.
  sample_rate = jack_get_sample_rate(client);
  octave_stdout << "|\n| JACK engine sample rate: " << sample_rate << " [Hz]" << std::endl;

  // Display the current JACK load.
  octave_stdout << "|\n| Current JACK engine CPU load: " << jack_cpu_load(client) << " [%]\n|" << std::endl;

  //
  // Create input and output ports.
  //

  input_port = jack_port_register(client, "input",
                                  JACK_DEFAULT_AUDIO_TYPE,
                                  JackPortIsInput, 0);

  output_port = jack_port_register(client, "output",
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsOutput, 0);

  if ((input_port == NULL) || (output_port == NULL)) {
    std::cerr << "No more JACK ports available!" << std::endl;
    return oct_retval;
  }

  // Tell the JACK server that we are ready to roll.
  // This segfaults of some reason?
  if (int err = jack_activate(client)) {

    std::cerr << "Activate err: " << err << std::endl;

    jack_client_close(client);

    // Bail out
    error("Cannot activate jack client");
  }

  // Connect the ports. Note: you can't do this before
  // the client is activated, because we can't allow
  // connections to be made to clients that aren't
  // running.

  // Connect output port -> input (playback) port

  if ((ports_o = jack_get_ports(client, NULL, NULL,
                                JackPortIsPhysical|JackPortIsOutput)) == NULL) {

    // Close connection and bail out.
    jack_client_close (client);
    error("Cannot find any physical output ports\n");
  }

  // Connect to first output (capture) port.
  if (jack_connect(client, ports_o[0], jack_port_name (input_port))) {

    // Close connection and bail out.
    jack_client_close (client);
    error("Cannot connect output ports\n");
  }

  //
  // Connect output (capture) port -> input port
  //

  if ((ports_i = jack_get_ports(client, NULL, NULL,
                                JackPortIsPhysical|JackPortIsInput)) == NULL) {

    // Disconnect outputs, close, and bail out
    jack_disconnect(client, ports_o[0], jack_port_name (input_port));
    jack_client_close (client);

    error("Cannot find any physical input ports\n");
  }

  // Connect to first input (playback) port.
  if (int err = jack_connect(client, jack_port_name(output_port), ports_i[0])) {

    std::cout << err << std::endl;

    // Disconnect outputs, close, and bail out
    jack_disconnect(client, ports_o[0], jack_port_name (input_port));
    jack_client_close (client);

    error("Cannot connect input ports\n");

    return oct_retval;
  }

  octave_stdout << "|------------------------------------------------------\n";
  octave_stdout << "|         Input ports:                                 \n";
  octave_stdout << "|------------------------------------------------------\n";
  n = 0;
  while(ports_i[n] != NULL) {

    port = jack_port_by_name(client, ports_i[n]);
    port_flags = jack_port_flags(port);

    if (strcmp("octave:jinfo:input", ports_i[n]) != 0) { // Don't print the jinfo input.
      if (port_flags & JackPortIsPhysical)
        octave_stdout << "|        " << ports_i[n] << " [physical]\n";
      else
        octave_stdout << "|        " << ports_i[n] << std::endl;
    }
    n++;
  }

  octave_stdout << "|------------------------------------------------------\n";
  octave_stdout << "|         Output ports:                                \n";
  octave_stdout << "|------------------------------------------------------\n";
  n = 0;
  while(ports_o[n] != NULL) {

    port = jack_port_by_name(client, ports_o[n]);
    port_flags = jack_port_flags(port);

    if (strcmp("octave:jinfo:output",ports_o[n]) != 0) { // Don't print the jinfo output.
      if (port_flags & JackPortIsPhysical)
        octave_stdout << "|        " << ports_o[n] << " [physical]\n";
      else
        octave_stdout << "|        " << ports_o[n] << std::endl;
    }
    n++;
  }
  octave_stdout << "|------------------------------------------------------\n";

  // Return sample rate if we have one  output arg.
  if (nlhs == 1) {
    Matrix fs_mat(1,1);
    double* fs_ptr = (double*) fs_mat.data();
    fs_ptr[0] = (double) sample_rate;
    oct_retval.append(fs_mat);
  }

  // Disconnect ports

  if (jack_disconnect(client, jack_port_name (output_port), ports_i[0])) {
    error("Cannot disconnect output port!\n");
  }

  if (jack_disconnect(client, ports_o[0], jack_port_name (input_port))) {
    error("Cannot connect input port!\n");
  }

  // Close the client.
  jack_client_close (client);

  // Clenaup
  if (ports_i) {
    free (ports_i);
  }
  if (ports_o) {
    free (ports_o);
  }

  return oct_retval;
}
