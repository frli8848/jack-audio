#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <asoundlib.h>
#include <sys/time.h>
#include <math.h>

int set_hwparams(snd_pcm_t *handle,
			snd_pcm_hw_params_t *params,
			snd_pcm_access_t access);
int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams);
int xrun_recovery(snd_pcm_t *handle, int err);
int write_loop(snd_pcm_t *handle,
		      signed short *samples,
		      snd_pcm_channel_area_t *areas);
int wait_for_poll(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count);
int write_and_poll_loop(snd_pcm_t *handle,
			       signed short *samples,
			       snd_pcm_channel_area_t *areas);
void async_callback(snd_async_handler_t *ahandler);
int async_loop(snd_pcm_t *handle,
		      signed short *samples,
		      snd_pcm_channel_area_t *areas);
void async_direct_callback(snd_async_handler_t *ahandler);
int async_direct_loop(snd_pcm_t *handle,
			     signed short *samples ATTRIBUTE_UNUSED,
			     snd_pcm_channel_area_t *areas ATTRIBUTE_UNUSED);
int direct_loop(snd_pcm_t *handle,
		       signed short *samples ATTRIBUTE_UNUSED,
		       snd_pcm_channel_area_t *areas ATTRIBUTE_UNUSED);
int direct_write_loop(snd_pcm_t *handle,
			     signed short *samples,
			     snd_pcm_channel_area_t *areas);

