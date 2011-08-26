
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
