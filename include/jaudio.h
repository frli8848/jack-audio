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

#ifndef __JAUDIO_H__
#define __JAUDIO_H__

#define FLOAT_AUDIO 0
#define DOUBLE_AUDIO 1

#include <jack/jack.h>

// Play

bool play_is_running(void);
void play_set_running_flag(void);
void play_clear_running_flag(void);

int play_finished(void);
int play_process_f(jack_nframes_t nframes, void *arg);
int play_process_d(jack_nframes_t nframes, void *arg);
int play_init(void* buffer, size_t frames, size_t channels,
              char **port_names, const char *client_name, int format);
int play_close(void);

// Record

bool got_a_trigger(void);
bool record_is_running(void);
void record_set_running_flag(void);
void record_clear_running_flag(void);

bool record_finished(void);
int record_init(void* buffer, size_t frames, size_t channels,
                char **port_names, const char *client_name);
int record_close(void);

bool t_record_finished(void);
int t_record_process_f(jack_nframes_t nframes, void *arg);
int t_record_process_d(jack_nframes_t nframes, void *arg);
int t_record_init(void* buffer, size_t frames, size_t channels,
                  char **port_names, const char *client_name,
                  double trigger_level,
                  size_t trigger_channel,
                  size_t trigger_frames,
                  size_t post_trigger_frames);
size_t get_ringbuffer_position(void);
int t_record_close(void);

//
// Play and record (duplex)
//

bool playrec_is_running(void);
void playrec_set_running_flag(void);
void playrec_clear_running_flag(void);

int playrec_srate(jack_nframes_t nframes, void *arg);
void playrec_jerror(const char *desc);
void playrec_jack_shutdown(void *arg);

int playrec_process_f(jack_nframes_t nframes, void *arg);
int playrec_process_d(jack_nframes_t nframes, void *arg);

bool playrec_finished(void);
int playrec_init(void* play_buffer, int play_format,
                 size_t play_channels, char **play_port_names,
                 void* record_buffer, size_t record_channels, char **record_port_names,
                 size_t frames,
                 const char *client_name,
                 size_t num_skip_buffers = 0);

int playrec_close(size_t play_channels,
                  char **play_port_names,
                  size_t record_channels,
                  char **record_port_names);

static void print_jack_status(jack_status_t status)
{

  if (status & JackFailure) {
    std::cout << "JackFailure: Overall operation failed." << std::endl;
  }

  if (status & JackNameNotUnique) {
    std::cout << "JackNameNotUnique" << std::endl;
  }

  if (status & JackServerStarted) {
    std::cout << "JackServerStarted" << std::endl;
  }

  if (status & JackServerFailed) {
    std::cout << "JackServerFailed" << std::endl;
  }

  if (status & JackServerError) {
    std::cout << "JackServerError" << std::endl;
  }

  if (status & JackNoSuchClient) {
    std::cout << "JackNoSuchClient" << std::endl;
  }

  if (status & JackLoadFailure) {
    std::cout << "JackLoadFailure" << std::endl;
  }

  if (status & JackInitFailure) {
    std::cout << "JackInitFailure" << std::endl;
  }

  if (status & JackShmFailure) {
    std::cout << "JackShmFailure" << std::endl;
  }

  if (status & JackVersionError) {
    std::cout << "JackVersionError" << std::endl;
  }

  if (status & JackBackendError) {
    std::cout << "JackBackendError" << std::endl;
  }

  if (status & JackClientZombie) {
    std::cout << "JackClientZombie" << std::endl;
  }
};

void jerror(const char *desc);
void jack_shutdown(void *arg);
int srate(jack_nframes_t nframes, void *arg);

#endif
