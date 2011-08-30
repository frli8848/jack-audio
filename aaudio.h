/***
 *
 * Copyright (C) 2007, 2008, 2009 Fredrik Lingvall
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

#ifndef __AAUDIO__
#define __AAUDIO__

#include <alsa/asoundlib.h>
#include <poll.h>


int is_running(void);
void set_running_flag(void);
void clear_running_flag(void);


int set_hwparams(snd_pcm_t *handle,
		 snd_pcm_format_t *format,
		 unsigned int *fs,
		 unsigned int *channels,
		 snd_pcm_uframes_t *period_size,
		 unsigned int *num_periods,
		 snd_pcm_uframes_t *buffer_size);

int set_swparams(snd_pcm_t *handle,
		 snd_pcm_uframes_t avail_min,
		 snd_pcm_uframes_t start_threshold,
		 snd_pcm_uframes_t stop_threshold);

void check_hw(snd_pcm_hw_params_t *hwparams);
int xrun_recovery(snd_pcm_t *handle, int err);

snd_pcm_sframes_t play_poll_loop(snd_pcm_t *handle,
				 unsigned int nfds, 
				 unsigned int poll_timeout,
				 pollfd *pfd,
				 snd_pcm_uframes_t period_size);

int write_and_poll_loop(snd_pcm_t *handle,
			const snd_pcm_channel_area_t *areas,
			snd_pcm_format_t format, 
			void *buffer,
			snd_pcm_sframes_t frames,
			snd_pcm_sframes_t framesize,
			unsigned int channels);


int read_and_poll_loop(snd_pcm_t *handle,
		       const snd_pcm_channel_area_t *record_areas,
		       snd_pcm_format_t format, 
		       void *buffer,
		       snd_pcm_sframes_t frames,
		       snd_pcm_sframes_t framesize,
		       unsigned int channels,
		       unsigned int wanted_channels);

snd_pcm_sframes_t 
t_read_and_poll_loop(snd_pcm_t *handle,
		     const snd_pcm_channel_area_t *record_areas,
		     snd_pcm_format_t format, 
		     void *buffer,
		     snd_pcm_sframes_t frames,
		     snd_pcm_sframes_t framesize,
		     unsigned int channels,
		     unsigned int wanted_channels,
		     double trigger_level,
		     int trigger_ch,
		     snd_pcm_sframes_t trigger_frames);


void pcm_list(void);
void device_list(int play_or_rec);

int is_interleaved(void);

#endif
