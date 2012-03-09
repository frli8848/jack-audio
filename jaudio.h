
/***
 *
 * Copyright (C) 2011,2012 Fredrik Lingvall
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

#ifndef __JAUDIO_H__
#define __JAUDIO_H__

#define FLOAT_AUDIO 0
#define DOUBLE_AUDIO 1


#include <jack/jack.h>

int got_a_trigger(void);
int is_running(void);
void set_running_flag(void);
void clear_running_flag(void);

int play_finished(void);
int play_process_f(jack_nframes_t nframes, void *arg);
int play_process_d(jack_nframes_t nframes, void *arg);
int play_init(void* buffer, octave_idx_type frames, octave_idx_type channels, 
	      char **port_names, const char *client_name, int format);
int play_close(void);

int record_finished(void);
int record_init(void* buffer, octave_idx_type frames, octave_idx_type channels, 
		char **port_names, const char *client_name);
int record_close(void);

int t_record_finished(void);
int t_record_process_f(jack_nframes_t nframes, void *arg);
int t_record_process_d(jack_nframes_t nframes, void *arg);
int t_record_init(void* buffer, octave_idx_type frames, octave_idx_type channels, 
		  char **port_names, const char *client_name,
		  double trigger_level,
		  octave_idx_type trigger_channel,
		  octave_idx_type trigger_frames,
		  octave_idx_type post_trigger_frames);
octave_idx_type get_ringbuffer_position(void);
int t_record_close(void);

#endif
