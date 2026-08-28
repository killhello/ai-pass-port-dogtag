#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void dogtag_audio_init(void);
void dogtag_audio_deinit(void);

void dogtag_audio_play_help(int volume);
void dogtag_audio_play_beep(void);
void dogtag_audio_find_me_request(void);
void dogtag_audio_stop(void);
void dogtag_audio_stop_clear(void);

#ifdef __cplusplus
}
#endif