#ifndef WAVEFORM_GENERATOR_H
#define WAVEFORM_GENERATOR_H

#include <gtk/gtk.h>
#include "parameter_store.h"
#include "scope_window.h"
#include "audio_manager.h"

#define SAMPLE_RATE 48000
#define BUFFER_SIZE 256
#define UPDATE_INTERVAL_MS 32

#define TARGET_FPS 60
#define FRAME_TIME_US (1000000 / TARGET_FPS)  // Convert to microseconds


typedef struct {
    float *data;
    size_t size;
    size_t write_pos;
    GMutex mutex;
    size_t target_cycles;  // Number of cycles to show on display
} DisplayBuffer;


typedef struct {
    ParameterStore *params;
    ScopeWindow *scope;
    AudioManager *audio;
    GThread *generator_thread;
    GMutex mutex;
    GCond cond;
    gboolean running;
    float phase;           // Current phase
    float fm_phase;        // FM modulation phase
    float am_phase;        // AM modulation phase
    float dcm_phase;       // Duty cycle modulation phase
    uint32_t sample_rate;  // Sample rate in Hz
    size_t buffer_size;    // Number of samples per update
} WaveformGenerator;

// Function declarations
WaveformGenerator* waveform_generator_create(ParameterStore *params, ScopeWindow *scope, AudioManager *audio);
void waveform_generator_destroy(WaveformGenerator *gen);
void waveform_generator_set_audio_enabled(WaveformGenerator *gen, bool enable);

#endif // WAVEFORM_GENERATOR_H
