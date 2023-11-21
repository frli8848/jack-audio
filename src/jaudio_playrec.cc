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

volatile bool playrec_running;

int total_playrec_frames;
int frames_played;
int frames_recorded;
bool is_first_jack_period;

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

bool playrec_is_running(void)
{
  return playrec_running;
}

void playrec_set_running_flag(void)
{
  playrec_running = true;

  return;
}

void playrec_clear_running_flag(void)
{
  playrec_running = false;

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


bool playrec_finished(void)
{
  return ((total_playrec_frames - frames_recorded) <= 0);
}

// Single precision play data.

int playrec_process_f(jack_nframes_t nframes, void *arg)
{
  float  **iobuffers = nullptr;
  float  *output_fbuffer = nullptr;
  float  *input_fbuffer = nullptr;
  jack_default_audio_sample_t *out = nullptr;
  jack_default_audio_sample_t *in = nullptr;

  // Get the adresses of the input and output buffers.
  iobuffers = (float**) arg;
  output_fbuffer = iobuffers[0];
  input_fbuffer = iobuffers[1];

  //
  // Play
  //

  // The number of available frames write.
  int frames_to_write = (int) nframes;

  if((total_playrec_frames - frames_played) > 0) {

    if (frames_to_write >  (total_playrec_frames - frames_played) ) {
      frames_to_write = total_playrec_frames - frames_played;
    }
  }

  // Loop over all ouput ports.
  for (size_t n=0; n<n_output_ports; n++) {

    // Grab the n:th output buffer.
    out = (jack_default_audio_sample_t *)
      jack_port_get_buffer(output_ports[n], nframes);

    if (out == nullptr) {
      std::cerr << "jack_port_get_buffer failed!" << std::endl;
      return -1;
    }

    // If the port was not closed fast enough after we were done playing
    // all frames then just fill jack's output buffers with silence.
    if (frames_played >= total_playrec_frames) {
      std::memset(out, 0x0, sizeof (jack_default_audio_sample_t) * nframes); // Just fill with silence.
    } else {

      if (playrec_running) {

        for (size_t m=0; m< (size_t) frames_to_write; m++) {
          out[(jack_nframes_t) m] = (jack_default_audio_sample_t)
            output_fbuffer[m+frames_played + n*total_playrec_frames];
        }

        // Fill the end with silence to avoid playing random buffer data.
        if ( frames_to_write < (int) nframes ) {
          for (size_t m=frames_to_write; m<nframes; m++) {
            out[(jack_nframes_t) m] = (jack_default_audio_sample_t) 0.0; // Silence.
          }
        }

      } // running
    }

  } // < n_output_ports

  if (frames_played < total_playrec_frames) {
    frames_played += frames_to_write;
  }

  //
  // Record (single precision)
  //

  // The number of available frames in the JACK buffer.
  int frames_to_read = (int) nframes;

  if ((total_playrec_frames - frames_recorded) > 0 && playrec_running) {

    // Check if the number of frames in JACK buffer is larger than what
    // we have left to read.
    if (frames_to_read >  (total_playrec_frames - frames_recorded) ) {
      frames_to_read = total_playrec_frames - frames_recorded;
    }

  } else {
    frames_recorded = total_playrec_frames;
    return 0;
  }

  // Loop over all input ports.
  for (size_t n=0; n< (size_t) n_input_ports; n++) {

    // Grab the n:th input buffer.
    in = (jack_default_audio_sample_t *)
      jack_port_get_buffer(input_ports[n], nframes);

    if (in == nullptr) {
      std::cerr << "jack_port_get_buffer failed!" << std::endl;
      return -1;
    }

    // If the port was not closed fast enough after we were done recording
    // then skip extra new frames.
    if (frames_recorded < total_playrec_frames) {

      if (playrec_running) {

        for (size_t m=0; m< (size_t) frames_to_read; m++) {
          input_fbuffer[m+frames_recorded + n*total_playrec_frames] = (float) in[(jack_nframes_t) m];
        }

      } // running

    }
  } // < n_input_ports

  frames_recorded += frames_to_read;

  return 0;
}

// Double precision play data (converted to float).

int playrec_process_d(jack_nframes_t nframes, void *arg)
{
  void  **iobuffers = nullptr;
  double  *output_dbuffer = nullptr;
  float  *input_fbuffer = nullptr; // JACK uses floats internally.
  jack_default_audio_sample_t *out = nullptr;
  jack_default_audio_sample_t *in = nullptr;

  // Get the adresses of the input and output buffers.
  iobuffers = (void**) arg;
  output_dbuffer = (double*) iobuffers[0];
  input_fbuffer = (float*) iobuffers[1];

  //
  // Play
  //

  // The number of available frames write.
  int frames_to_write = (int) nframes;

  if((total_playrec_frames - frames_played) > 0) {

    if (frames_to_write >  (total_playrec_frames - frames_played) ) {
      frames_to_write = total_playrec_frames - frames_played;
    }
  }

  // Loop over all ouput ports.
  for (size_t n=0; n< (size_t) n_output_ports; n++) {

    // Grab the n:th output buffer.
    out = (jack_default_audio_sample_t *)
      jack_port_get_buffer(output_ports[n], nframes);

    if (out == nullptr) {
      std::cerr << "jack_port_get_buffer failed!" << std::endl;
      return -1;
    }

    // If the port was not closed fast enough after we were done playing
    // all frames then just fill jack's output buffers with silence.
    if (frames_played >= total_playrec_frames) {
      std::memset(out, 0x0, sizeof (jack_default_audio_sample_t) * nframes); // Just fill with silence.
    } else {

      if (playrec_running) {

        for (size_t m=0; m< (size_t)frames_to_write; m++) {
          out[(jack_nframes_t) m] = (jack_default_audio_sample_t) // double -> float conversion
            output_dbuffer[m+frames_played + n*total_playrec_frames];
        }

        // Fill the end with silence to avoid playing random buffer data.
        if ( frames_to_write < (int) nframes ) {
          for (size_t m=frames_to_write; m < (size_t) nframes; m++) {
            out[(jack_nframes_t) m] = (jack_default_audio_sample_t) 0.0; // Silence.
          }
        }

      } // running
    }

  } // < n_output_ports

  if (frames_played < total_playrec_frames) {
    frames_played += frames_to_write;
  }

  //
  // Record (single precision)
  //

  // The number of available frames.
  int frames_to_read = (int) nframes;

  if (frames_to_read >  (total_playrec_frames - frames_recorded) ) {
    frames_to_read = total_playrec_frames - frames_recorded;
  }

  // Loop over all input ports.
  for (size_t n=0; n<n_input_ports; n++) {

    // Grab the n:th input buffer.
    in = (jack_default_audio_sample_t *)
      jack_port_get_buffer(input_ports[n], nframes);

    if (in == nullptr) {
      std::cerr << "jack_port_get_buffer failed!" << std::endl;
      return -1;
    }

    // If the port was not closed fast enough after we were done recording
    // then skip extra new frames.
    if (frames_recorded < total_playrec_frames) {

      if (playrec_running) {

        for (size_t m=0; m< (size_t) frames_to_read; m++) {
          input_fbuffer[m+frames_recorded + n*total_playrec_frames] = (float) in[(jack_nframes_t) m];
        }

      } // running

    }
  } // < n_input_ports

  frames_recorded += frames_to_read;

  return 0;
}

/***
 *
 * playrec_init
 *
 * Init the record client, connect to the jack input ports, and start recording audio data.
 *
 ***/

int playrec_init(void* play_buffer, int play_format,
                 size_t play_channels, char **play_port_names,
                 void* record_buffer, size_t record_channels, char **record_port_names,
                 size_t frames,
                 const char *client_name)
{
  char port_name[255];

  n_output_ports = play_channels;
  n_input_ports = record_channels;

  // The total number of frames to play and record.
  total_playrec_frames = frames;

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
  if (play_format == DOUBLE_AUDIO) {
    jack_set_process_callback(playrec_client, playrec_process_d, buffer_array);
  }

  if (play_format == FLOAT_AUDIO) {
    jack_set_process_callback(playrec_client, playrec_process_f, buffer_array);
  }

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

  for (size_t n=0; n<n_input_ports; n++) {
    sprintf(port_name,"input_%d",(int) n+1); // Port numbers start at 1.
    input_ports[n] = jack_port_register(playrec_client, port_name,
                                        JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  }

  // output ports

  output_ports = (jack_port_t**) malloc(n_output_ports * sizeof(jack_port_t*));

  for (size_t n=0; n<n_output_ports; n++) {
    sprintf(port_name,"output_%d", (int) n+1); // Port numbers start at 1.
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
  for (size_t n=0; n<n_input_ports; n++) {
    if (jack_connect(playrec_client, record_port_names[n], jack_port_name(input_ports[n]))) {
      std::cerr << "Cannot connect to the client output port: '" <<  record_port_names[n] << "'" << std::endl;
      return -1;
    }
  }

  // Connect to the output ports.
  for (size_t n=0; n<n_output_ports; n++) {
    if (jack_connect(playrec_client, jack_port_name(output_ports[n]), play_port_names[n])) {
      std::cerr << "Cannot connect to the client output port: '" <<  play_port_names[n] << "'" << std::endl;
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
  int err;

  //
  //  Record ports
  //

  // Disconnect to the input ports.
  for (size_t n=0; n<n_input_ports; n++) {
    if (jack_disconnect(playrec_client, record_port_names[n], jack_port_name(input_ports[n]))) {
      std::cerr << "Cannot connect to the client output port: '" <<  record_port_names[n] << "'" << std::endl;
      return -1;
    }
  }

  // Unregister all input ports.
  for (size_t n=0; n<n_input_ports; n++) {
    err = jack_port_unregister(playrec_client, input_ports[n]);
    if (err) {
      std::cerr << "Failed to unregister an input port!" << std::endl;
    }
  }

  //
  //  Play ports
  //

  // Disconnect to the output ports.
  for (size_t n=0; n<n_output_ports; n++) {
    if (jack_disconnect(playrec_client, jack_port_name(output_ports[n]), play_port_names[n])) {
      std::cerr << "Cannot connect to the client output port: '" <<  play_port_names[n] << "'" << std::endl;
      return -1;
    }
  }

  // Unregister all output ports.
  for (size_t n=0; n<n_output_ports; n++) {
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
