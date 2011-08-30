
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
// Globals.
//

volatile int running;

octave_idx_type play_frames;
octave_idx_type record_frames;

octave_idx_type frames_played;
octave_idx_type frames_recorded;

jack_client_t *record_client;
jack_port_t **input_ports;
int n_input_ports;

jack_client_t *play_client;
jack_port_t **output_ports;
octave_idx_type n_output_ports;

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
  octave_idx_type   frames_to_write, n, m;
  double   *output_dbuffer;
  jack_default_audio_sample_t *out;

  // Get the adress of the output buffer.
  output_dbuffer = (double*) arg;

  // The number of available frames.
  frames_to_write = (octave_idx_type) nframes;
  
  // Loop over all ports.
  for (n=0; n<n_output_ports; n++) {

    // Grab the n:th output buffer.
    out = (jack_default_audio_sample_t *) 
      jack_port_get_buffer(output_ports[n], nframes);

    if (out == NULL)
      error("jack_port_get_buffer failed!");
    
    if((play_frames - frames_played) > 0 && running) { 
      
      if (frames_to_write >  (play_frames - frames_played) )
	frames_to_write = play_frames - frames_played; 


      for(m=0; m<frames_to_write; m++)
	out[(jack_nframes_t) m] = (jack_default_audio_sample_t)
	  output_dbuffer[m+frames_played + n*play_frames];
    } else {
      frames_played = play_frames; 
      return 0;
    }
    
  }
  
  frames_played += frames_to_write;
  
  return 0;
}


//
//  Init the play client, connect to the jack input ports, and start playing audio data.
//

int play_init(void* buffer, octave_idx_type frames, int channels, char **port_names) 
{
  int n;
  jack_port_t  *port;
  char port_name[255];

  // The number of channels (columns) in the buffer matrix.
  n_output_ports = (octave_idx_type) channels;

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

  for (n=0; n<n_output_ports; n++) { 
    sprintf(port_name,"output_%d",n+1); // Port numbers start at 1.
    output_ports[n] = jack_port_register(play_client, port_name, 
					 JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  }

  // Tell the JACK server that we are ready to roll.
  if (jack_activate(play_client)) {
    error("Cannot activate jack client");
    return -1;
  }

  // Connect to the input ports.  
  for (n=0; n<n_output_ports; n++) {
    if (jack_connect(play_client, jack_port_name(output_ports[n]), port_names[n])) {
      error("Cannot connect to the client input port '%s'\n",port_names[n]);
      play_close();
      return -1;
    }
  }

  return 0;
}


int play_close(void)
{
  int n, err;
   // Unregister all ports for the play client.
  for (n=0; n<n_output_ports; n++) {
    err = jack_port_unregister(play_client, output_ports[n]);
    if (err)
      error("Failed to unregister an output port");
  }
 
  // Close the client.
  err = jack_client_close(play_client);
  if (err)
    error("jack_client_close failed");

  free(output_ports);

  return 0;
}

// ********************************************************************************************

int record_finished(void)
{

  return ((record_frames - frames_recorded) <= 0);
}

//
// The record callback function.
//

int record_process(jack_nframes_t nframes, void *arg)
{
  octave_idx_type   frames_to_read, n, m;
  double   *input_dbuffer;
  jack_default_audio_sample_t *in;

  // Get the adress of the input buffer.
  input_dbuffer = (double*) arg;

  // The number of available frames.
  frames_to_read = (octave_idx_type) nframes;
  
  // Loop over all ports.
  for (n=0; n<n_input_ports; n++) {

    // Grab the n:th input buffer.
    in = (jack_default_audio_sample_t *) 
      jack_port_get_buffer(input_ports[n], nframes);

    if (in == NULL)
      error("jack_port_get_buffer failed!");
    
    if((record_frames - frames_recorded) > 0 && running) { 
      
      if (frames_to_read >  (record_frames - frames_recorded) )
	frames_to_read = record_frames - frames_recorded; 

      for(m=0; m<frames_to_read; m++)
	input_dbuffer[m+frames_recorded + n*record_frames] = (double) in[(jack_nframes_t) m];
      
    } else {
      frames_recorded = record_frames; 
      return 0;
    }
    
  }
  
  frames_recorded += frames_to_read;
  
  return 0;
}


//
//  Init the record client, connect to the jack input ports, and start recording audio data.
//

int record_init(void* buffer, octave_idx_type frames, int channels, char **port_names) 
{
  int n;
  jack_port_t  *port;
  char port_name[255];

  // The number of channels (columns) in the buffer matrix.
  n_input_ports = (octave_idx_type) channels;

  // The total number of frames to record.
  record_frames = frames;

  // Reset record counter.
  frames_recorded = 0;

  // Tell the JACK server to call jerror() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.
  // 
  // This is set here so that it can catch errors in the
  // connection process.
  jack_set_error_function (jerror);

  // Try to become a client of the JACK server.
  if ((record_client = jack_client_new ("octave:jrecord")) == 0) {
    error("jack server not running?\n");
    return -1;
  }

  // Tell the JACK server to call the `record_process()' whenever
  // there is work to be done.
  jack_set_process_callback(record_client, record_process, buffer);
  
  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  jack_set_sample_rate_callback(record_client, srate, 0);
  
  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown(record_client, jack_shutdown, 0);

  input_ports = (jack_port_t**) malloc(n_input_ports * sizeof(jack_port_t*));

  for (n=0; n<n_input_ports; n++) { 
    sprintf(port_name,"input_%d",n+1); // Port numbers start at 1.
    input_ports[n] = jack_port_register(record_client, port_name, 
					 JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  }

  // Tell the JACK server that we are ready to roll.
  if (jack_activate(record_client)) {
    error("Cannot activate jack client");
    return -1;
  }

  // Connect to the input ports.  
  for (n=0; n<n_input_ports; n++) {
    if (jack_connect(record_client, port_names[n], jack_port_name(input_ports[n]))) {
      error("Cannot connect to the client output port '%s'\n",port_names[n]);
      record_close();
      return -1;
    }
  }

  return 0;
}


int record_close(void)
{
  int n, err;
   // Unregister all ports for the record client.
  for (n=0; n<n_input_ports; n++) {
    err = jack_port_unregister(record_client, input_ports[n]);
    if (err)
      error("Failed to unregister an input port");
  }
 
  // Close the client.
  err = jack_client_close(record_client);
  if (err)
    error("jack_client_close failed");

  free(input_ports);

  return 0;
}


// ********************************************************************************************


double *triggerbuffer = NULL;
int    triggerport = 0;
double trigger_level = 1.0e16, trigger = 0.0;
int    trigger_active;

int ringbuffer_read_running;
octave_idx_type ringbuffer_position = 0;
octave_idx_type post_trigger_frames = 0;
int has_wrapped;


int t_record_finished(void)
{
  return !ringbuffer_read_running;
}


//
// The triggered record callback function.
//

int t_record_process(jack_nframes_t nframes, void *arg)
{
  octave_idx_type frames_to_read, n, m, m2;
  double   *input_dbuffer;
  jack_default_audio_sample_t *in;

  //
  // Read data from JACK and save it in the ring buffer.
  //

  // Get the adress of the input buffer.
  input_dbuffer = (double*) arg;

  // The number of available frames.
  frames_to_read = (octave_idx_type) nframes;
  

  if ( running && ringbuffer_read_running ) { 

    // Loop over all JACK ports.
    for (n=0; n<n_input_ports; n++) {
      
      // Grab the n:th input buffer.
      in = (jack_default_audio_sample_t *) 
	jack_port_get_buffer(input_ports[n], nframes);
      
      if (in == NULL)
	error("jack_port_get_buffer failed!");
      
      for(m=0; m<frames_to_read; m++) {
	
	ringbuffer_position += m;
	
	if ( ringbuffer_position >= record_frames) { // Check if we have exceeded the size of the ring buffer. 
	  ringbuffer_position = 0; // We have reached the end of the ringbuffer so start from 0 again.
	  has_wrapped = TRUE; // To indicated that the ring buffer has been full.
	}	
	
	input_dbuffer[ringbuffer_position + n*record_frames] = (double) in[(jack_nframes_t) m];	  
      }
      
    }
    
    //
    // Update the triggerbuffer
    //
    
    if (n == triggerport) {
      
      if (!trigger_active) { // TODO: We may have a problem if frames_to_read >= trigger_frames!
	
	// 1) "Forget" the old data which is now shifted out of the trigger buffer.
	// We have got 'frames_to_read' new frames so forget the 'frames_to_read' oldest ones. 
	
	for (m=0; m<frames_to_read; m++) {
	  
	  m2 = (trigger_position + m) % trigger_frames;
	  
	  trigger -= fabs(triggerbuffer[m2]);
	}
	
	// 2) Add the new data to the trigger ring buffer.
	
	for (m=0; m<frames_to_read; m++) {
	  
	  m2 = (trigger_position + m) % trigger_frames;
	  
	  triggerbuffer[m2] = input_dbuffer[(frames_recorded + m)*channels + trigger_ch];  
	  
	}
	
	// 3) Update the trigger value.
	
	for (m=0; m<frames_to_read; m++) {
	  
	  m2 = (trigger_position + m) % trigger_frames;
	  
	  trigger += fabs(triggerbuffer[m2]);
	}
	
	// 4) Set the new position in the trigger buffer.
	
	trigger_position = m2 + 1;	
	
	// Check if we are above the threshold.
	if ( (trigger / (double) trigger_frames) > trigger_level) {
	  trigger_active = TRUE;
	  
	  struct tm *the_time;
	  time_t curtime;
	  
	  // Get the current time.
	  curtime = time(NULL);
	  
	  // Convert it to local time representation. 
	  the_time = localtime(&curtime);
	  
	  // This should work with Octave's diary command.
	  octave_stdout << "\n Got a trigger signal at: " << asctime (the_time) << "\n";
	  
	}
	
      } else { // We have already detected a signal just wait until we have got all the requested data. 
	post_trigger_frames += frames_to_read; // Add the number of acquired frames.
      }
      
      
      // We have got a trigger. Now wait for record_frames/2 more data and then
      // we're done acquiring data.
      if (trigger_active && (post_trigger_frames >= record_frames/2) )
	ringbuffer_read_running = FALSE; // Exit the read loop.
      
    } // if (n == triggerport)
    
  } // if ( running && ringbuffer_read_running )
  
  return 0;
}




/***
 *
 * t_record_init
 *
 * Function that continuously read the audio stream and 
 * saves that data in a ring buffer when the input signal
 * is over the trigger level.
 *
 *
 * Returns the position of the last acquired frame in 
 * the ring buffer.
 *
 ***/

int t_record_init(void* buffer, octave_idx_type frames, int channels, char **port_names,
		  double trigger_level,
		  int trigger_ch,
		  octave_idx_type trigger_frames)
{
  int n;
  jack_port_t  *port;
  char port_name[255];

  // The number of channels (columns) in the buffer matrix.
  n_input_ports = (octave_idx_type) channels;

  // The total number of frames to record.
  record_frames = frames;

  // Reset record counter.
  frames_recorded = 0;

  // Tell the JACK server to call jerror() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.
  // 
  // This is set here so that it can catch errors in the
  // connection process.
  jack_set_error_function (jerror);

  // Try to become a client of the JACK server.
  if ((record_client = jack_client_new ("octave:jtrecord")) == 0) {
    error("jack server not running?\n");
    return -1;
  }

  // Tell the JACK server to call the `record_process()' whenever
  // there is work to be done.
  jack_set_process_callback(record_client, record_process, buffer);
  
  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  jack_set_sample_rate_callback(record_client, srate, 0);
  
  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown(record_client, jack_shutdown, 0);

  // Flag used to stop the data acquisition.
  int ringbuffer_read_running = TRUE;
  
  // Initialze the ring buffer position.
  octave_idx_type ringbuffer_position = 0;

  // Allocate space and clear the trigger buffer.
  triggerbuffer = (double*) malloc(trigger_frames*sizeof(double));
  bzero(triggerbuffer, trigger_frames*sizeof(double));

  // Reset the wrapped flag.
  has_wrapped = FALSE;

  // Initialize trigger values.
  trigger = 0.0;
  trigger_position = 0;
  trigger_active = FALSE;

 // This should work with Octave's diary command.
  octave_stdout << "\n Audio capturing started. Listening to JACK port " << port_names[trigger_ch]  << " for a trigger signal.\n\n";

  input_ports = (jack_port_t**) malloc(n_input_ports * sizeof(jack_port_t*));

  for (n=0; n<n_input_ports; n++) { 
    sprintf(port_name,"input_%d",n+1); // Port numbers start at 1.
    input_ports[n] = jack_port_register(record_client, port_name, 
					 JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  }

  // Tell the JACK server that we are ready to roll.
  if (jack_activate(record_client)) {
    error("Cannot activate jack client");
    return -1;
  }

  // Connect to the input ports.  
  for (n=0; n<n_input_ports; n++) {
    if (jack_connect(record_client, port_names[n], jack_port_name(input_ports[n]))) {
      error("Cannot connect to the client output port '%s'\n",port_names[n]);
      record_close();
      return -1;
    }
  }
  
  // Note that the data is not sequential in time in the buffer, that is,
  // the buffer must be shifted (if wrapped) by the calling function/program.
  
  // If the ring buffer never has been wrapped (been full) then we should not shift it.
  if (!has_wrapped) 
    ringbuffer_position = 0;

  //
  // Cleanup
  //

  free(triggerbuffer);

  return 0;
}

octave_idx_type get_ringbuffer_position(void)
{
  return ringbuffer_position;
}
