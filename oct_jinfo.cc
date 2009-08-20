/***
 *
 * Copyright (C) 2009 Fredrik Lingvall 
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

#include <jack/jack.h>

//#include <alsa/asoundlib.h>
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

int process (jack_nframes_t nframes, void *arg)
{
  // Just copy input to output.

  jack_default_audio_sample_t *out = 
    (jack_default_audio_sample_t *) 
    jack_port_get_buffer (output_port, nframes);

  jack_default_audio_sample_t *in = 
    (jack_default_audio_sample_t *) 
    jack_port_get_buffer (input_port, nframes);
  
  memcpy (out, in, sizeof (jack_default_audio_sample_t) * nframes);
  
  return 0;      
}

// This is called whenever the sample rate changes.
int srate (jack_nframes_t nframes, void *arg)
{
  //printf("The sample rate is now %lu/sec.\n", (long unsigned int) nframes);
  return 0;
}

void jerror (const char *desc)
{
  error("JACK error: %s\n", desc);
}

void jack_shutdown (void *arg)
{
  // Do nothing
  return;
}


  
/***
 * 
 * Octave (oct) gateway function for JINFO.
 *
 ***/

DEFUN_DLD (jinfo, args, nlhs,
	   "-*- texinfo -*-\n\
@deftypefn {Loadable Function} {}  jinfo(dev_name).\n\
\n\
JINFO Prints various hardware info of the JACK device\n\
\n\
@copyright{} 2009 Fredrik Lingvall.\n\
@seealso {aplay, arecord, aplayrec, @indicateurl{http://jackaudio.org}}\n\
@end deftypefn")
{
  jack_client_t *client;
  const char **ports_i, **ports_o;
  char device[50];
  int  buflen,n;
  octave_value_list oct_retval; // Octave return (output) parameters

  int nrhs = args.length ();

  // Check for proper inputs arguments.
  
  if (nrhs > 1) {
    error("jinfo don't have more than one input argument!");
    return oct_retval;
  }
  
  if (nlhs > 0) {
    error("jinfo don't have output arguments!");
    return oct_retval;
  }
  
  //
  //  The jack audio device.
  //
  
  if (nrhs == 1) {
    
    if (!mxIsChar(0)) {
      error("1st arg (the audio device) must be a string !");
      return oct_retval;
    }
    
    std::string strin = args(0).string_value(); 
    buflen = strin.length();
    for (n=0; n<=buflen; n++ ) {
      device[n] = strin[n];
    }
    device[buflen] = '\0';
    
  } else
    strcpy(device,"default"); 
  
  //
  // List all devices if no input arg is given.
  //
  
#if 0
  if (nrhs < 1) {
    printf("No ALSA device was given. Listing the devices:\n\n");
    //device_list(1); // Playback.
    printf("\n");
    //device_list(0); // Capture.
    return oct_retval;
  }
  
#endif

  // Tell the JACK server to call error() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.
  // 
  // This is set here so that it can catch errors in the
  // connection process.
  jack_set_error_function (jerror);

  // Try to become a client of the JACK server.
  if ((client = jack_client_new ("octave:jinfo")) == 0) {
    error("jack server not running?\n");
    return oct_retval;
  }

  // Tell the JACK server to call `process()' whenever
  // there is work to be done.
  jack_set_process_callback (client, process, 0);
  
  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  jack_set_sample_rate_callback (client, srate, 0);
  
  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown (client, jack_shutdown, 0);
  
  // Display the current sample rate. once the client is activated 
  // (see below), you should rely on your own sample rate
  // callback (see above) for this value.
  printf ("\n Engine sample rate: %lu\n\n", 
	  (long unsigned int) jack_get_sample_rate (client));
  
  //
  // Create two ports.
  //

  input_port = jack_port_register(client, "input", 
				  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  
  output_port = jack_port_register(client, "output", 
				   JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  
  // Tell the JACK server that we are ready to roll.
  if (jack_activate (client)) {
    error("Cannot activate jack client");
    return oct_retval;
  }
  
  // Connect the ports. Note: you can't do this before
  // the client is activated, because we can't allow
  // connections to be made to clients that aren't
  // running.
  if ((ports_o = jack_get_ports(client, NULL, NULL, 
			      JackPortIsPhysical|JackPortIsOutput)) == NULL) {
    error("Cannot find any physical capture ports\n");
    return oct_retval;
  }
  
  //if (jack_connect(client, ports_o[0], jack_port_name (input_port))) 
  //  error("Cannot connect input ports\n");
  
  //free (ports_o);
  
  if ((ports_i = jack_get_ports(client, NULL, NULL, 
			      JackPortIsPhysical|JackPortIsInput)) == NULL) {
    error("Cannot find any physical playback ports\n");
    return oct_retval;
  }
  
  //if ((ports_o = jack_get_ports(client, NULL, NULL, 
  //			      JackPortIsPhysical|JackPortIsOutput)) == NULL) {
  //  error("Cannot find any physical capture ports\n");
  //  return oct_retval;
  //}

  //if (jack_connect(client, ports[0], jack_port_name (input_port))) 
  //  error("Cannot connect input ports\n");
  
  //if ((ports_i = jack_get_ports (client, NULL, NULL, 
  //                             JackPortIsPhysical|JackPortIsInput)) == NULL) {
  //  error("Cannot find any physical playback ports\n");
  //  return oct_retval;
  //}

  printf("|------------------------------------------------------\n");
  printf("|         Physical input ports                         \n");
  printf("|------------------------------------------------------\n");
  n = 0;
  while(ports_i[n] != NULL) {
    printf("|        %s\n", ports_i[n]);
    n++;
  }
  printf("|------------------------------------------------------\n\n\n");


  printf("|------------------------------------------------------\n");
  printf("|         Physical output ports                        \n");
  printf("|------------------------------------------------------\n");
  n = 0;
  while(ports_o[n] != NULL) {
    printf("|        %s\n", ports_o[n]);
    n++;
  }
  printf("|------------------------------------------------------\n");

  free (ports_i);
  free (ports_o);

  // Close the client.
  jack_client_close (client);
  
  return oct_retval;
}
