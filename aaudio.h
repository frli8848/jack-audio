
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
		       unsigned int channels);

void pcm_list(void);
void device_list(void);

int is_interleaved(void);
