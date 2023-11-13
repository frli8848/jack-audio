/***
 *
 * Copyright (C) 2011,2012, 2023 Fredrik Lingvall
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

#include <octave/oct.h>

#include <iostream>
using namespace std;

#include <octave/defun-dld.h>
#include <octave/error.h>

#include <octave/pager.h>
#include <octave/symtab.h>
#include <octave/variables.h>

#include "jaudio.h"

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

volatile int got_data;

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

void jerror(const char *desc)
{
  error("JACK error: %s\n", desc);
  clear_running_flag(); // Stop if we get a JACK error.

  return;
}

void jack_shutdown(void *arg)
{
  clear_running_flag(); // Stop if JACK shuts down..

  return;
}

void print_jack_status(jack_status_t status)
{
    
  if (status & JackFailure) {
    octave_stdout << "JackFailure: Overall operation failed." << endl;
  }
  
  if (status & JackNameNotUnique) {
    octave_stdout << "JackNameNotUnique" << endl;
  }
  
  if (status & JackServerStarted) {
    octave_stdout << "JackServerStarted" << endl;
  }
  
  if (status & JackServerFailed) {
    octave_stdout << "JackServerFailed" << endl;
  }
  
  if (status & JackServerError) {
    octave_stdout << "JackServerError" << endl;
  }
  
  if (status & JackNoSuchClient) {
    octave_stdout << "JackNoSuchClient" << endl;
  }
  
  if (status & JackLoadFailure) {
    octave_stdout << "JackLoadFailure" << endl;
  }
  
  if (status & JackInitFailure) {
    octave_stdout << "JackInitFailure" << endl;
  }
  
  if (status & JackShmFailure) {
    octave_stdout << "JackShmFailure" << endl;
  }
  
  if (status & JackVersionError) {
    octave_stdout << "JackVersionError" << endl;
  }
  
  if (status & JackBackendError) {
    octave_stdout << "JackBackendError" << endl;
  }
  
  if (status & JackClientZombie) {
    octave_stdout << "JackClientZombie" << endl;
  }
}

/********************************************************************************************
*
* Audio Playback
*
*********************************************************************************************/

/***
 *
 * play_finished
 *
 * To check if we have played all audio data.
 *
 ***/

int play_finished(void)
{
  return ((play_frames - frames_played) <= 0);
}

/***
 *
 * play_process
 *
 * The play callback function.
 *
 ***/

// Single precision data.

int play_process_f(jack_nframes_t nframes, void *arg)
{
  octave_idx_type   frames_to_write, n, m;
  float   *output_fbuffer;
  jack_default_audio_sample_t *out;
  
  // Get the adress of the output buffer.
  output_fbuffer = (float*) arg;

  // The number of available frames.
  frames_to_write = (octave_idx_type) nframes;
  
  // Loop over all ports.
  for (n=0; n<n_output_ports; n++) {

    // Grab the n:th output buffer.
    out = (jack_default_audio_sample_t *)
      jack_port_get_buffer(output_ports[n], nframes);

    if (out == nullptr) {
      error("jack_port_get_buffer failed!");
    }
    
    if((play_frames - frames_played) > 0 && running) { 
      
      if (frames_to_write >  (play_frames - frames_played) ) {
	frames_to_write = play_frames - frames_played; 
      }

      for(m=0; m<frames_to_write; m++) {
	out[(jack_nframes_t) m] = (jack_default_audio_sample_t)
	  output_fbuffer[m+frames_played + n*play_frames];
      }
    } else {
      frames_played = play_frames; 
      return 0;
    }
    
  }
  
  frames_played += frames_to_write;
  
  return 0;
}

// Double precision data.

int play_process_d(jack_nframes_t nframes, void *arg)
{
  octave_idx_type   frames_to_write, n, m;
  double   *output_fbuffer;
  jack_default_audio_sample_t *out;

  // Get the adress of the output buffer.
  output_fbuffer = (double*) arg;

  // The number of available frames.
  frames_to_write = (octave_idx_type) nframes;
  
  // Loop over all ports.
  for (n=0; n<n_output_ports; n++) {

    // Grab the n:th output buffer.
    out = (jack_default_audio_sample_t *)
      jack_port_get_buffer(output_ports[n], nframes);

    if (out == nullptr) {
      error("jack_port_get_buffer failed!");
    }
    
    if((play_frames - frames_played) > 0 && running) { 
      
      if (frames_to_write >  (play_frames - frames_played) ) {
	frames_to_write = play_frames - frames_played; 
      }

      for(m=0; m<frames_to_write; m++) {
	out[(jack_nframes_t) m] = (jack_default_audio_sample_t)
	  output_fbuffer[m+frames_played + n*play_frames];
      }
    } else {
      frames_played = play_frames; 
      return 0;
    }
    
  }
  
  frames_played += frames_to_write;
  
  return 0;
}

/***
 *
 * play_init
 * 
 * Init the play client, connect to the jack input ports, and start playing audio data.
 *
 ***/

int play_init(void* buffer, octave_idx_type frames, octave_idx_type channels, 
	      char **port_names, const char *client_name, int format) 
{
  octave_idx_type n;
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
  jack_set_error_function(jerror);

  // Try to become a client of the JACK server.
  jack_status_t status;
  if ((play_client = jack_client_open(client_name,
				      JackNullOption,&status)) == 0) {
    print_jack_status(status);
    error("Failed to open JACK client: '%s'\n",client_name);
    return -1;
  }

  // Tell the JACK server to call the `play_process()' whenever
  // there is work to be done.
  if (format == FLOAT_AUDIO) {
    jack_set_process_callback(play_client, play_process_f, buffer);
  }
  
  if (format == DOUBLE_AUDIO) {
    jack_set_process_callback(play_client, play_process_d, buffer);
  }
  
  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  jack_set_sample_rate_callback(play_client, srate, 0);
  
  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown(play_client, jack_shutdown, 0);

  output_ports = (jack_port_t**) malloc(n_output_ports * sizeof(jack_port_t*));

  for (n=0; n<n_output_ports; n++) { 
    sprintf(port_name,"output_%d", (int) n+1); // Port numbers start at 1.
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

/***
 *
 * play_close
 *
 * Close the JACK clients and free port 
 * memory.
 *
 ***/

int play_close(void)
{
  octave_idx_type n;
  int err;
   // Unregister all ports for the play client.
  for (n=0; n<n_output_ports; n++) {
    err = jack_port_unregister(play_client, output_ports[n]);
    if (err) {
      error("Failed to unregister an output port");
    }
  }
 
  // Close the client.
  err = jack_client_close(play_client);
  if (err) {
    error("jack_client_close failed");
  }

  free(output_ports);

  return 0;
}

/********************************************************************************************
*
* Audio Capturing
*
*********************************************************************************************/

/***
 *
 * record_finished
 *
 * To check if we have read all audio data.
 *
 ***/

int record_finished(void)
{

  return ((record_frames - frames_recorded) <= 0);
}

/***
 *
 * record_process
 *
 * The record callback function.
 *
 ***/

int record_process(jack_nframes_t nframes, void *arg)
{
  octave_idx_type   frames_to_read, n, m;
  float   *input_fbuffer;
  jack_default_audio_sample_t *in;

  // Get the adress of the input buffer.
  input_fbuffer = (float*) arg;

  // The number of available frames.
  frames_to_read = (octave_idx_type) nframes;
  
  // Loop over all ports.
  for (n=0; n<n_input_ports; n++) {

    // Grab the n:th input buffer.
    in = (jack_default_audio_sample_t *)
      jack_port_get_buffer(input_ports[n], nframes);
    
    if (in == nullptr) {
      error("jack_port_get_buffer failed!");
    }
    
    if((record_frames - frames_recorded) > 0 && running) { 
      
      if (frames_to_read >  (record_frames - frames_recorded) ) {
	frames_to_read = record_frames - frames_recorded;
      }

      for(m=0; m<frames_to_read; m++) {
	input_fbuffer[m+frames_recorded + n*record_frames] = (float) in[(jack_nframes_t) m];
      }
      
    } else {
      frames_recorded = record_frames; 
      return 0;
    }
    
  }
  
  frames_recorded += frames_to_read;
  
  return 0;
}

/***
 *
 * record_init
 *
 * Init the record client, connect to the jack input ports, and start recording audio data.
 *
 ***/

int record_init(void* buffer, octave_idx_type frames, octave_idx_type channels, 
		char **port_names, const char *client_name) 
{
  octave_idx_type n;
  jack_port_t *port;
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
  jack_set_error_function(jerror);

  // Try to become a client of the JACK server.
  jack_status_t status;
  if ((record_client = jack_client_open(client_name,
					JackNullOption,&status)) == 0) {
    print_jack_status(status);
    error("Failed to open JACK client: '%s'\n",client_name);
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
    sprintf(port_name,"input_%d",(int) n+1); // Port numbers start at 1.
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

/***
 *
 * record_close
 *
 * Close the JACK clients and free port 
 * memory.
 *
 ***/

int record_close(void)
{
  octave_idx_type n;
  int err;

  // Unregister all ports for the record client.
  for (n=0; n<n_input_ports; n++) {
    err = jack_port_unregister(record_client, input_ports[n]);
    if (err) {
      error("Failed to unregister an input port");
    }
  }
 
  // Close the client.
  err = jack_client_close(record_client);
  if (err) {
    error("jack_client_close failed");
  }

  if (input_ports) {
    free(input_ports);
  } else {
    error("Failed free input_ports memory");
  }
  
  return 0;
}


/********************************************************************************************
*
* Triggered Audio Capturing
*
*********************************************************************************************/

// Globals for the triggered audio caputring.

float *triggerbuffer = NULL;
octave_idx_type triggerport;
float t_level = 1.0, trigger = 0.0;
int    trigger_active;
octave_idx_type trigger_position;
octave_idx_type t_frames;

int ringbuffer_read_running;
octave_idx_type ringbuffer_position;
octave_idx_type post_t_frames_counter;
octave_idx_type post_t_frames;
int has_wrapped;

/***
 *
 * t_record_finished
 *
 * To check if we are listening and recording audio data.
 *
 ***/

int t_record_finished(void)
{
  return !ringbuffer_read_running;
}

/***
 *
 * t_record_process
 *
 * The JACK callback function for triggered recording.
 *
 ***/

int t_record_process(jack_nframes_t nframes, void *arg)
{
  octave_idx_type frames_to_read, n, m, m2;
  octave_idx_type local_rbuf_pos;
  float   *input_fbuffer;
  jack_default_audio_sample_t *in;

  // Get the adress of the input buffer.
  input_fbuffer = (float*) arg;

  // The number of available frames.
  frames_to_read = (octave_idx_type) nframes;

  if ( running && ringbuffer_read_running ) { 

    // Loop over all JACK ports.
    for (n=0; n<n_input_ports; n++) {
      
      // We need to keep a local (per channel) ringbuffer index.
      local_rbuf_pos = ringbuffer_position;

      // Grab the n:th input buffer.
      in = (jack_default_audio_sample_t *) 
	jack_port_get_buffer(input_ports[n], nframes);
      
      if (in == nullptr) {
	error("jack_port_get_buffer failed!");
      }

      //
      // Read data from JACK and save it in the ring buffer.
      //
      
      for(m=0; m<frames_to_read; m++) {

	if (local_rbuf_pos >= record_frames) { // Check if we have exceeded the size of the ring buffer. 
	  local_rbuf_pos = 0; // We have reached the end of the ringbuffer so start from 0 again.
	  has_wrapped = true; // Indicate that the ring buffer is full.
	}	
	
	input_fbuffer[local_rbuf_pos + n*record_frames] = (float) in[(jack_nframes_t) m];
	
	local_rbuf_pos++; // Inrease the ring buffer position for the next audio sample.	

      }

      //
      // Update the trigger buffer
      //
      
      if (n == triggerport) {

	if (!trigger_active) { // TODO: We may have a problem if frames_to_read >= t_frames!
	  
	  // 1) "Forget" the old data which is now shifted out of the trigger buffer.
	  // We have got 'frames_to_read' new frames so forget the 'frames_to_read' oldest ones. 

	  for (m=0; m<frames_to_read; m++) {

	    m2 = (trigger_position + m) % t_frames;

	    trigger -= fabsf(triggerbuffer[m2]);
	  }

	  // 2) Add the new data to the trigger ring buffer.
	  
	  for (m=0; m<frames_to_read; m++) {
	    
	    m2 = (trigger_position + m) % t_frames;
	    
	    triggerbuffer[m2] = (float) in[(jack_nframes_t) m];
	  }

	  // 3) Update the trigger value.
	  
	  for (m=0; m<frames_to_read; m++) {
	    
	    m2 = (trigger_position + m) % t_frames;
	    
	    trigger += fabsf(triggerbuffer[m2]);
	  }

	  // 4) Set the new position in the trigger buffer.
	  
	  trigger_position = m2 + 1;	
	  
	  // Check if we are above the threshold.
	  if ( (trigger / (float) t_frames) > t_level) {
	    trigger_active = true;
	    
	    struct tm *the_time;
	    time_t curtime;
	    
	    // Get the current time.
	    curtime = time(NULL);
	    
	    // Convert it to local time representation. 
	    the_time = localtime(&curtime);
	    
	    // This should work with Octave's diary command.
	    octave_stdout << "\n Got a trigger signal at: " << asctime (the_time) << "\n";
	    got_data = true;	    
	  }
	  
	} else { // We have already detected a signal so wait until we have got all the requested data. 
	  post_t_frames_counter += frames_to_read; // Add the number of acquired frames.
	}

	// We have got a trigger and the buffer has wrapped. Now wait for post_t_frames more 
	// data and then we're done acquiring data.
	if (trigger_active && has_wrapped && (post_t_frames_counter >= post_t_frames)) {
	  ringbuffer_read_running = false; // Exit the read loop.
	}
	
	// We have got a trigger and the buffer has NOT wrapped. Now just wait until the buffer
	// is full. This is to avoid saving a non-full buffer. If the buffer wraps while we
	// are waiting for record_frames number of frames (= until the ringbuffer is full)
	// then the condition above applies and we wait for post_t_frames number of frames instead.
	if (trigger_active && !has_wrapped && (local_rbuf_pos >= record_frames)) {
	  ringbuffer_read_running = false; // Exit the read loop.
	}
	
      } // if (n == triggerport)
      
    } // for (n=0; n<n_input_ports; n++) 

    ringbuffer_position = local_rbuf_pos; // Update the global ringbuffer position index.
    
  } // if ( running && ringbuffer_read_running )
  
  return 0;
}

/***
 *
 * got_a_trigger
 *
 * Function that returns the trigger status.
 *
 *
 ***/

int got_a_trigger(void)
{
  return got_data;
}

/***
 *
 * t_record_init
 *
 * Function that initializes continuous read of the audio stream and
 * then waits for a trigger signal. Audio data is saved continously 
 * in a ring buffer and the data quisition is stopped when 
 * post_trigger_frames frames have been aquired after the input 
 * signal average level is over the trigger_level. The average 
 * signal level is computed from a (typically) smaller ring buffer
 * of trigger_frames length.
 *
 * The audio data is read by the JACK callback function 
 * t_record_process() above.
 *
 ***/

int t_record_init(void* buffer, octave_idx_type frames, octave_idx_type channels, 
		  char **port_names, const char *client_name,
		  double trigger_level,
		  octave_idx_type trigger_channel,
		  octave_idx_type trigger_frames,
		  octave_idx_type post_trigger_frames)
{
  octave_idx_type n;
  jack_port_t  *port;
  char port_name[255];

  // Clear trigger indicator.
  got_data = false;

  // The number of channels (columns) in the buffer matrix.
  n_input_ports = (octave_idx_type) channels;

  // Set the trigger level for the callback function.
  t_level = (float) trigger_level;

  // The total number of frames to record.
  record_frames = frames;

  // Reset record counter.
  frames_recorded = 0;

  // Reset the post trigger counter.
  post_t_frames_counter = 0;
  post_t_frames = post_trigger_frames;

  // Flag used to stop the data acquisition.
  ringbuffer_read_running = true;
  
  // Initialze the ring buffer position.
  ringbuffer_position = 0;

  // Allocate space and clear the trigger buffer.
  triggerbuffer = (float*) malloc(trigger_frames*sizeof(float));
  if (!triggerbuffer) {
    error("Trigger buffer memory allocation failed!\n");
    return -1;
  }
  bzero(triggerbuffer, trigger_frames*sizeof(float));

  t_frames = trigger_frames;

  // Reset the wrapped flag.
  has_wrapped = false;

  // Initialize trigger parameters.
  trigger = 0.0;	    // Clear the trigger value.
  trigger_position = 0;	    // Start from the beginning of the buffer.
  trigger_active = false;   // Clear the trigger status.
  triggerport = trigger_channel; // The (global) trigger port for the JACK callback function.

  if (triggerport < 0 || triggerport >= n_input_ports) {
    error("Trigger channel out-of-bounds!\n");
    return -1;
  }

  // Tell the JACK server to call jerror() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.
  // 
  // This is set here so that it can catch errors in the
  // connection process.
  jack_set_error_function (jerror);

  // Try to become a client of the JACK server.
  jack_status_t status;
  if ((record_client = jack_client_open(client_name,
					JackNullOption,&status)) == 0) {
    print_jack_status(status);
    error("Failed to open JACK client: '%s'\n",client_name);
    return -1;
  }

  // Tell the JACK server to call the `record_process()' whenever
  // there is work to be done.
  jack_set_process_callback(record_client, t_record_process, buffer);
  
  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  jack_set_sample_rate_callback(record_client, srate, 0);
  
  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown(record_client, jack_shutdown, 0);
 
  // Register the input ports.  
  input_ports = (jack_port_t**) malloc(n_input_ports * sizeof(jack_port_t*));
  for (n=0; n<n_input_ports; n++) { 
    sprintf(port_name,"input_%d",(int) n+1); // Port numbers start at 1.
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
      t_record_close();
      return -1;
    }
  }
  
  // This should work with Octave's diary command.
  octave_stdout << "\n Audio capturing started. Listening to JACK port '" << 
    port_names[trigger_channel]  << "' for a trigger signal.\n\n";

  return 0;
}


/***
 *
 * get_ringbuffer_position
 *
 * Returns the position of the last acquired frame in 
 * the ring buffer. This function should only be called
 * after the data has been aquired.
 *
 ***/

octave_idx_type get_ringbuffer_position(void)
{
  
  // Note that the data is not sequential in time in the buffer, that is,
  // the buffer must be shifted (if wrapped) by the calling function/program.
  
  // If the ring buffer never has been wrapped (never been full) then we should not shift it.
  if (!has_wrapped) 
    ringbuffer_position = 0;

  return ringbuffer_position;
}

/***
 *
 * t_record_close
 *
 * Close the JACK clients and free port 
 * and trigger buffer memory.
 *
 ***/

int t_record_close(void)
{
  octave_idx_type n;
  int err;

   // Unregister all ports for the record client.
  for (n=0; n<n_input_ports; n++) {
    err = jack_port_unregister(record_client, input_ports[n]);
    if (err) {
      error("Failed to unregister an input port");
    }
  }
 
  // Close the client.
  err = jack_client_close(record_client);
  if (err) {
    error("jack_client_close failed");
  }

  //
  // Cleanup memory.
  //

  if(input_ports) {
    free(input_ports);
  } else {
    error("Failed free input_ports memory");
  }

  if (triggerbuffer) {
    free(triggerbuffer);
  } else {
    error("Failed free triggerbuffer memory");
  }
  
  return 0;
}
