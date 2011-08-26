
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

//
// Globals.
//

volatile int running;

size_t play_frames;
size_t rec_frames;

size_t frames_played;
size_t frames_recorded;

jack_client_t *rec_client;
jack_port_t **input_ports;
int n_input_ports;

jack_client_t *play_client;
jack_port_t **output_ports;
int n_output_ports;

/***
 *
 * Functions for CTRL-C support.
 *
 */

int is_running(void)
{

  return running;
}


void set_running_flag(void)
{
  running = 1;
  
  return;
}

void clear_running_flag(void)
{
  running = 0;
  
  return;
}


// This is called whenever the sample rate changes.
int srate(jack_nframes_t nframes, void *arg)
{
  //printf("The sample rate is now %lu/sec.\n", (long unsigned int) nframes);
  return 0;
}

void jerror (const char *desc)
{
  error("JACK error: %s\n", desc);
}

void jack_shutdown(void *arg)
{
  // Do nothing
  return;
}

int play_finished(void)
{

  return ((play_frames - frames_played) <= 0);
}

//
// The play callback function.
//

int play_process(jack_nframes_t nframes, void *arg)
{
  size_t   frames_to_write, m;
  size_t   n;
  double   *output_buffer;
  jack_default_audio_sample_t *out;

  // Get the adress of the output buffer.
  output_buffer = (double*) arg;

  // The number of available frames.
  frames_to_write = (size_t) nframes;
  
  // Loop over all ports.
  for (n=0; n<n_output_ports; n++) {

    // Grab the n:th output buffer.
    out = (jack_default_audio_sample_t *) 
      jack_port_get_buffer(output_ports[n], nframes);
    
    if((play_frames - frames_played) > 0 && running) { 
      
      if (frames_to_write >  (play_frames - frames_played) )
	frames_to_write = play_frames - frames_played; 

      for(m=0; m<frames_to_write; m++)
	out[m] = (jack_default_audio_sample_t) 
	  output_buffer[m+frames_played + n*play_frames];
      
    }
  }
  
  frames_played += frames_to_write;
  
  return 0;
}


//
//  Init the play client, connect to the output ports, and start playing audio data.
//

int play_init(void* buffer, size_t frames, int channels, char **port_names) 
{
  int n;
  jack_port_t  *port;
  char port_name[255];

  // The number of channels (columns) in the buffer matrix.
  n_output_ports = channels;

  // The total number of frames to play.
  play_frames = frames;

  // Reset play counter.
  frames_played = 0;

  // Tell the JACK server to call jerror() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.
  // 
  // This is set here so that it can catch errors in the
  // connection process.
  jack_set_error_function (jerror);

  // Try to become a client of the JACK server.
  if ((play_client = jack_client_new ("octave:jplay")) == 0) {
    error("jack server not running?\n");
    return -1;
  }

  // Tell the JACK server to call the `play_process()' whenever
  // there is work to be done.
  jack_set_process_callback(play_client, play_process, buffer);
  
  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  jack_set_sample_rate_callback(play_client, srate, 0);
  
  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown(play_client, jack_shutdown, 0);
  
  output_ports = (jack_port_t**) malloc(n_output_ports * sizeof(jack_port_t*));
  
  for (n=1; n<=n_output_ports; n++) { // Port numbers start at 1.
    sprintf(port_name,"output_%d",n);
    output_ports[n] = jack_port_register(play_client, port_name, 
					 JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  }

  // Tell the JACK server that we are ready to roll.
  if (jack_activate(play_client)) {
    error("Cannot activate jack client");
    return -1;
  }

  // Connect to the output ports.  
  for (n=0; n<n_output_ports; n++) {
    if (jack_connect(play_client, jack_port_name(output_ports[n]), port_names[n])) {
      error("Cannot connect to the output port %s\n",output_ports[n]);
      play_close();
      return -1;
    }
  }

  return 0;
}


int play_close(void)
{
  // Close the client.
  jack_client_close(play_client);

  free(output_ports);

  return 0;
}

