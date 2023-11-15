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
#include <math.h>
#include <pthread.h>
#include <signal.h>

#include <iostream>
#include <cstring>

#include "jaudio.h"

//
// Globals.
//

volatile int play_running;

size_t play_frames;
size_t frames_played;

jack_client_t *play_client;
jack_port_t **output_ports;
size_t n_output_ports;

/***
 *
 * Functions for CTRL-C support.
 *
 */

int play_is_running(void)
{
  return play_running;
}

void play_set_running_flag(void)
{
  play_running = 1;

  return;
}

void play_clear_running_flag(void)
{
  play_running = 0;

  return;
}

// This is called whenever the sample rate changes.
int play_srate(jack_nframes_t nframes, void *arg)
{
  //printf("The sample rate is now %lu/sec.\n", (long unsigned int) nframes);
  return 0;
}

void play_jerror(const char *desc)
{
  std::cerr << "JACK error: '" << desc << "'" << std::endl;
  play_clear_running_flag(); // Stop if we get a JACK error.

  return;
}

void play_jack_shutdown(void *arg)
{
  play_clear_running_flag(); // Stop if JACK shuts down..

  return;
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
  size_t   frames_to_write, n, m;
  float   *output_fbuffer;
  jack_default_audio_sample_t *out;

  // Get the adress of the output buffer.
  output_fbuffer = (float*) arg;

  // The number of available frames.
  frames_to_write = (size_t) nframes;

  if((play_frames - frames_played) > 0) {

    if (frames_to_write >  (play_frames - frames_played) ) {
      frames_to_write = play_frames - frames_played;
    }
  }

  // Loop over all ports.
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
    if (frames_played >= play_frames) {
      std::memset(out, 0x0, sizeof (jack_default_audio_sample_t) * nframes); // Just fill with silence.
    } else {

      if(play_running) {

        for(m=0; m<frames_to_write; m++) {
          out[(jack_nframes_t) m] = (jack_default_audio_sample_t)
            output_fbuffer[m+frames_played + n*play_frames];
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

  if (frames_played < play_frames) {
    frames_played += frames_to_write;
  }

  return 0;
}

// Double precision data.

int play_process_d(jack_nframes_t nframes, void *arg)
{
  size_t   frames_to_write, n, m;
  double   *output_dbuffer;
  jack_default_audio_sample_t *out;

  // Get the adress of the output buffer.
  output_dbuffer = (double*) arg;

  // The number of available frames.
  frames_to_write = (size_t) nframes;

  if((play_frames - frames_played) > 0) {

    if (frames_to_write >  (play_frames - frames_played) ) {
      frames_to_write = play_frames - frames_played;
    }
  }

  // Loop over all ports.
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
    if (frames_played >= play_frames) {
      std::memset(out, 0x0, sizeof (jack_default_audio_sample_t) * nframes); // Just fill with silence.
    } else {

      if(play_running) {

        for(m=0; m<frames_to_write; m++) {
          out[(jack_nframes_t) m] = (jack_default_audio_sample_t)
            output_dbuffer[m+frames_played + n*play_frames];
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

  if (frames_played < play_frames) {
    frames_played += frames_to_write;
  }

  return 0;
}

/***
 *
 * play_init
 *
 * Init the play client, connect to the jack input ports, and start playing audio data.
 *
 ***/

int play_init(void* buffer, size_t frames, size_t channels,
              char **port_names, const char *client_name, int format)
{
  size_t n;
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
  jack_set_error_function(play_jerror);

  // Try to become a client of the JACK server.
  jack_status_t status;
  if ((play_client = jack_client_open(client_name,
                                      JackNullOption,&status)) == 0) {
    print_jack_status(status);
    std::cerr << "Failed to open JACK client: '" << client_name << "'!" << std::endl;
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
  jack_set_sample_rate_callback(play_client, play_srate, 0);

  // Tell the JACK server to call `jack_shutdown()' if
  // it ever shuts down, either entirely, or if it
  // just decides to stop calling us.
  jack_on_shutdown(play_client, play_jack_shutdown, 0);

  output_ports = (jack_port_t**) malloc(n_output_ports * sizeof(jack_port_t*));

  for (n=0; n<n_output_ports; n++) {
    sprintf(port_name,"output_%d", (int) n+1); // Port numbers start at 1.
    output_ports[n] = jack_port_register(play_client, port_name,
                                         JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  }

  // Tell the JACK server that we are ready to roll.
  if (jack_activate(play_client)) {
    std::cerr << "Cannot activate jack client!" << std::endl;
    return -1;
  }

  // Connect to the output ports.
  for (n=0; n<n_output_ports; n++) {
    if (jack_connect(play_client, jack_port_name(output_ports[n]), port_names[n])) {
      std::cerr << "Cannot connect to the client output port: '" <<  port_names[n] << "'" << std::endl;
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
  size_t n;
  int err;
  // Unregister all ports for the play client.
  for (n=0; n<n_output_ports; n++) {
    err = jack_port_unregister(play_client, output_ports[n]);
    if (err) {
      std::cerr << "Failed to unregister an output port!" << std::endl;
    }
  }

  // Close the client.
  err = jack_client_close(play_client);
  if (err) {
    std::cerr << "jack_client_close failed!" << std::endl;
      }

  free(output_ports);

  return 0;
}
