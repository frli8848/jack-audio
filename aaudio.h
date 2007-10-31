#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <asoundlib.h>
#include <sys/time.h>
#include <math.h>

static int set_hwparams(snd_pcm_t *handle,
			snd_pcm_hw_params_t *params,
			snd_pcm_access_t access);
static int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams);
static int xrun_recovery(snd_pcm_t *handle, int err);
static int write_loop(snd_pcm_t *handle,
		      signed short *samples,
		      snd_pcm_channel_area_t *areas);
static int wait_for_poll(snd_pcm_t *handle, struct pollfd *ufds, unsigned int count);
static int write_and_poll_loop(snd_pcm_t *handle,
			       signed short *samples,
			       snd_pcm_channel_area_t *areas);
static void async_callback(snd_async_handler_t *ahandler);
static int async_loop(snd_pcm_t *handle,
		      signed short *samples,
		      snd_pcm_channel_area_t *areas);
static void async_direct_callback(snd_async_handler_t *ahandler);
static int async_direct_loop(snd_pcm_t *handle,
			     signed short *samples ATTRIBUTE_UNUSED,
			     snd_pcm_channel_area_t *areas ATTRIBUTE_UNUSED);
static int direct_loop(snd_pcm_t *handle,
		       signed short *samples ATTRIBUTE_UNUSED,
		       snd_pcm_channel_area_t *areas ATTRIBUTE_UNUSED);
static int direct_write_loop(snd_pcm_t *handle,
			     signed short *samples,
			     snd_pcm_channel_area_t *areas);

