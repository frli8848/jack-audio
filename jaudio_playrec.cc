/***
 *
 * Copyright (C) 2023 Fredrik Lingvall
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
#include <math.h>
#include <pthread.h>
#include <signal.h>

#include <iostream>
#include <cstring>

#include "jaudio.h"

//
// Globals.
//

volatile int playrec_running;

size_t playrec_frames;
size_t frames_played;
size_t frames_recorded;

jack_client_t *playrec_client;

jack_port_t **input_ports;
size_t n_input_ports;

jack_port_t **output_ports;
size_t n_output_ports;

/***
 *
 * Functions for CTRL-C support.
 *
 */

int playrec_is_running(void)
{
  return playrec_running;
}

void playrec_set_running_flag(void)
{
  playrec_running = 1;

  return;
}

void playrec_clear_running_flag(void)
{
  playrec_running = 0;

  return;
}

// This is called whenever the sample rate changes.
int playrec_srate(jack_nframes_t nframes, void *arg)
{
  //printf("The sample rate is now %lu/sec.\n", (long unsigned int) nframes);
  return 0;
}

void playrec_jerror(const char *desc)
{
  std::cerr << "JACK error: '" << desc << "'" << std::endl;
  playrec_clear_running_flag(); // Stop if we get a JACK error.

  return;
}

void playrec_jack_shutdown(void *arg)
{
  playrec_clear_running_flag(); // Stop if JACK shuts down..

  return;
}

// Single precision play data.

int playrec_process_f(jack_nframes_t nframes, void *arg)
{
  size_t n=0, m=0;
  float  **fbuffers = nullptr;
  float  *output_fbuffer = nullptr;
  float  *input_fbuffer = nullptr;
  jack_default_audio_sample_t *out = nullptr;
  jack_default_audio_sample_t *in = nullptr;

  // Get the adresses of the input and output buffers.
  fbuffers = (float**) arg;
  output_fbuffer = fbuffers[0];
  input_fbuffer = fbuffers[1];

  //
  // Play
  //

  // The number of available frames write.
  size_t frames_to_write = (size_t) nframes;

  if((playrec_frames - frames_played) > 0) {

    if (frames_to_write >  (playrec_frames - frames_played) ) {
      frames_to_write = playrec_frames - frames_played;
    }
  }

  // Loop over all ouput ports.
  for (n=0; n<n_output_ports; n++) {

    // Grab the n:th output buffer.
    out = (jack_default_audio_sample_t *)
      jack_port_get_buffer(output_ports[n], nframes);

    if (out == nullptr) {
      std::cerr << "jack_port_get_buffer failed!" << std::endl;
      return -1;
    }

    // If the port was not closed fast enough after we were done playing
    // all frames then just fill jack's output buffers with silence.
    if (frames_played >= playrec_frames) {
      std::memset(out, 0x0, sizeof (jack_default_audio_sample_t) * nframes); // Just fill with silence.
    } else {

      if(playrec_running) {

        for(m=0; m<frames_to_write; m++) {
          out[(jack_nframes_t) m] = (jack_default_audio_sample_t)
            output_fbuffer[m+frames_played + n*playrec_frames];
        }

        // Fill the end with silence to avoid playing random buffer data.
        if ( frames_to_write < nframes ) {
          for(m=frames_to_write; m<nframes; m++) {
            out[(jack_nframes_t) m] = (jack_default_audio_sample_t) 0.0; // Silence.
          }
        }

      } // running
    }

  } // < n_output_ports

  if (frames_played < playrec_frames) {
    frames_played += frames_to_write;
  }

  //
  // Record
  //

  // The number of available frames.
  size_t frames_to_read = (size_t) nframes;

  if (frames_to_read >  (playrec_frames - frames_recorded) ) {
    frames_to_read = playrec_frames - frames_recorded;
  }

  // Loop over all input ports.
  for (n=0; n<n_input_ports; n++) {

    // Grab the n:th input buffer.
    in = (jack_default_audio_sample_t *)
      jack_port_get_buffer(input_ports[n], nframes);

    if (in == nullptr) {
      std::cerr << "jack_port_get_buffer failed!" << std::endl;
      return -1;
    }

    // If the port was not closed fast enough after we were done recording
    // then skip extra new frames.
    if (frames_recorded < playrec_frames) {

      if(playrec_running) {

        for(m=0; m<frames_to_read; m++) {
          input_fbuffer[m+frames_recorded + n*playrec_frames] = (float) in[(jack_nframes_t) m];
        }

      } // running

    }
  } // < n_input_ports

  if (frames_recorded < playrec_frames) {
    frames_recorded += frames_to_read;
  }

  // We are done.
  if (frames_recorded >= playrec_frames) {
    playrec_clear_running_flag();
  }

  return 0;
}

/***
 *
 * playrec_init
 *
 * Init the record client, connect to the jack input ports, and start recording audio data.
 *
 ***/

int playrec_init(void* play_buffer,  size_t play_channels, char **play_port_names,
                 void* record_buffer, size_t record_channels, char **record_port_names,
                 size_t frames,
                 const char *client_name)
{
  size_t n;
  jack_port_t *port;
  char port_name[255];

  n_output_ports = (size_t) play_channels;
  n_input_ports = (size_t) record_channels;

  // The total number of frames to play and record.
  playrec_frames = frames;

  // Reset record counter.
  frames_played = 0;
  frames_recorded = 0;

  // Tell the JACK server to call jerror() whenever it
  // experiences an error.  Notice that this callback is
  // global to this process, not specific to each client.
  //
  // This is set here so that it can catch errors in the
  // connection process.
  jack_set_error_function(playrec_jerror);

  // Try to become a client of the JACK server.
  jack_status_t status;
  if ((playrec_client = jack_client_open(client_name,
                                        JackNullOption,&status)) == 0) {
    print_jack_status(status);
    std::cerr << "Failed to open JACK client: '" << client_name << "'" << std::endl;
    return -1;
  }

  // Tell the JACK server to call the `record_process()' whenever
  // there is work to be done.
  void *buffer_array[2];
  buffer_array[0] = play_buffer;
  buffer_array[1] = record_buffer;
  jack_set_process_callback(playrec_client, playrec_process_f, buffer_array);

  // Tell the JACK server to call `srate()' whenever
  // the sample rate of the system changes.
  jack_set_sample_rate_callback(playrec_client, playrec_srate, 0);

  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown(playrec_client, playrec_jack_shutdown, 0);

  //
  // Register ports
  //

  // Input ports

  input_ports = (jack_port_t**) malloc(n_input_ports * sizeof(jack_port_t*));

  for (n=0; n<n_input_ports; n++) {
    sprintf(port_name,"input_%d",(int) n+1); // Port numbers start at 1.
    input_ports[n] = jack_port_register(playrec_client, port_name,
                                        JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  }

  // output ports

  output_ports = (jack_port_t**) malloc(n_output_ports * sizeof(jack_port_t*));

  for (n=0; n<n_output_ports; n++) {
    sprintf(port_name,"output_%d",(int) n+1); // Port numbers start at 1.
    output_ports[n] = jack_port_register(playrec_client, port_name,
                                        JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  }

  //
  // Tell the JACK server that we are ready to roll.
  //

  if (jack_activate(playrec_client)) {
    std::cerr << "Cannot activate jack client!" << std::endl;
    return -1;
  }

  //
  // Connect the ports
  //

  // Connect to the input ports.
  for (n=0; n<n_input_ports; n++) {
    if (jack_connect(playrec_client, record_port_names[n], jack_port_name(input_ports[n]))) {
      std::cerr << "Cannot connect to the client output port: '" <<  record_port_names[n] << "'" << std::endl;
      record_close();
      return -1;
    }
  }

  // Connect to the output ports.
  for (n=0; n<n_output_ports; n++) {
    if (jack_connect(playrec_client, jack_port_name(output_ports[n]), play_port_names[n])) {
      std::cerr << "Cannot connect to the client output port: '" <<  play_port_names[n] << "'" << std::endl;
      record_close();
      return -1;
    }
  }

  return 0;
}

/***
 *
 * playrec_close
 *
 * Disconnect the JACK ports, close the JACK client, and free memory.
 *
 ***/

int playrec_close(size_t play_channels,
                  char **play_port_names,
                  size_t record_channels,
                  char **record_port_names)
{
  size_t n;
  int err;

  //
  //  Record ports
  //

  // Disconnect to the input ports.
  for (n=0; n<n_input_ports; n++) {
    if (jack_disconnect(playrec_client, record_port_names[n], jack_port_name(input_ports[n]))) {
      std::cerr << "Cannot connect to the client output port: '" <<  record_port_names[n] << "'" << std::endl;
      record_close();
      return -1;
    }
  }

  // Unregister all input ports.
  for (n=0; n<n_input_ports; n++) {
    err = jack_port_unregister(playrec_client, input_ports[n]);
    if (err) {
      std::cerr << "Failed to unregister an input port!" << std::endl;
    }
  }

  //
  //  Play ports
  //

  // Disconnect to the output ports.
  for (n=0; n<n_output_ports; n++) {
    if (jack_disconnect(playrec_client, jack_port_name(output_ports[n]), play_port_names[n])) {
      std::cerr << "Cannot connect to the client output port: '" <<  play_port_names[n] << "'" << std::endl;
      play_close();
      return -1;
    }
  }

  // Unregister all output ports.
  for (n=0; n<n_output_ports; n++) {
    err = jack_port_unregister(playrec_client, output_ports[n]);
    if (err) {
      std::cerr << "Failed to unregister an output port!" << std::endl;
    }
  }

  //
  // Close the playrec client.
  //

  err = jack_client_close(playrec_client);
  if (err) {
    std::cerr << "jack_client_close failed!" << std::endl;
  }

  //
  // Free buffers.
  //

  if (input_ports) {
    free(input_ports);
  } else {
    std::cerr << "Failed free input_ports memory!" << std::endl;
  }

  if (output_ports) {
    free(output_ports);
  } else {
    std::cerr << "Failed free output_ports memory!" << std::endl;
  }

  return 0;
}
