#include "dogtag_audio.h"
#include "dogtag_state.h"
#include "bsp_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>


extern const uint8_t dogtag_help_wav_start[] asm("_binary_dogtag_help_wav_start");
extern const uint8_t dogtag_help_wav_end[]   asm("_binary_dogtag_help_wav_end");

static TaskHandle_t s_audio_task;
static volatile bool s_audio_stop;
static volatile bool s_find_me_request;

static void play_wav(int volume) {
    const uint8_t *data = dogtag_help_wav_start;
    size_t len = (size_t)(dogtag_help_wav_end - dogtag_help_wav_start);
    if (len < 44 || memcmp(data, "RIFF", 4) != 0) return;

    size_t pos = 12;
    const uint8_t *pcm = NULL;
    uint32_t pcm_len = 0;
    while (pos + 8 <= len) {
        uint32_t cs;
        memcpy(&cs, data + pos + 4, sizeof(cs));
        if (memcmp(data + pos, "data", 4) == 0) {
            pcm = data + pos + 8;
            pcm_len = cs;
            break;
        }
        pos += 8 + cs;
    }
    if (!pcm) return;

    bsp_audio_set_format(16000, 16, 1);
    bsp_audio_set_volume(volume);
    size_t written = 0;
    while (written < pcm_len && !s_audio_stop) {
        size_t n = (pcm_len - written) > 4096 ? 4096 : (pcm_len - written);
        bsp_audio_write((void *)(pcm + written), n);
        written += n;
    }
}

static void play_beep(void) {
    if (s_audio_stop) return;
    bsp_audio_set_format(8000, 16, 1);
    bsp_audio_set_volume(30);
    int16_t buf[800];
    for (int i = 0; i < 800; i++)
        buf[i] = ((i % 8) < 4) ? 3000 : -3000;
    bsp_audio_write(buf, sizeof(buf));
}

static void audio_task(void *arg) {
    (void)arg;
    for (;;) {
        if (s_find_me_request) {
            s_find_me_request = false;
            play_wav(90);
            continue;
        }
        dogtag_state_t state = dogtag_state_get_state();
        if (state == DOGTAG_STATE_MILD_LOST && !s_audio_stop) {
            play_beep();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (state == DOGTAG_STATE_SEVERE_LOST && !s_audio_stop) {
            play_wav(90);
            if (s_audio_stop) continue;
            for (int i = 0; i < 3 && !s_audio_stop; i++) {
                play_beep();
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void dogtag_audio_init(void) {
    s_audio_stop = true;
    s_find_me_request = false;
    if (!s_audio_task) {
        xTaskCreate(audio_task, "dogtag_audio", 4096, NULL, 3, &s_audio_task);
    }
}

void dogtag_audio_deinit(void) {
    s_audio_stop = true;
    if (s_audio_task) {
        vTaskDelete(s_audio_task);
        s_audio_task = NULL;
    }
}

void dogtag_audio_play_help(int volume) {
    play_wav(volume);
}

void dogtag_audio_play_beep(void) {
    play_beep();
}

void dogtag_audio_find_me_request(void) {
    s_find_me_request = true;
}

void dogtag_audio_stop(void) {
    s_audio_stop = true;
}

void dogtag_audio_stop_clear(void) {
    s_audio_stop = false;
}