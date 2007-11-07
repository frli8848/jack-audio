
#include <alsa/asoundlib.h>
#include <poll.h>


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
