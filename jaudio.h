
#include <jack/jack.h>

int is_running(void);
void set_running_flag(void);
void clear_running_flag(void);

int play_finished(void);
int play_init(void* buffer, size_t frames, int channels, char **port_names);
int play_close(void);

int record_finished(void);
int record_init(void* buffer, size_t frames, int channels, char **port_names);
int record_close(void);


int t_record_finished(void);
int t_record_process(jack_nframes_t nframes, void *arg);
int t_record_init(void* buffer, octave_idx_type frames, int channels, char **port_names,
		  double trigger_level,
		  octave_idx_type trigger_channel,
		  octave_idx_type trigger_frames);
octave_idx_type get_ringbuffer_position(void);
