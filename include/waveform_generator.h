#ifndef WAVEFORM_GENERATOR_H
#define WAVEFORM_GENERATOR_H

#include <gtk/gtk.h>
#include <stdbool.h>

// Forward declarations
struct ParameterStore;
struct ScopeWindow;
struct AudioManager;

#define SAMPLE_RATE 48000
#define BUFFER_SIZE 256
#define UPDATE_INTERVAL_MS 32

#define TARGET_FPS 60
#define FRAME_TIME_US (1000000 / TARGET_FPS)  // Convert to microseconds

#define FILTER_STAGES 4

typedef struct {
    float cutoff;
    float resonance;
    float stage[FILTER_STAGES];
    float delay[FILTER_STAGES];
    float cutoff_mod;
    float res_mod;
    float cutoff_lfo_phase;
    float res_lfo_phase;
} LadderFilter;

struct WaveformGenerator {
    struct ParameterStore *params;
    struct ScopeWindow *scope;
    struct AudioManager *audio;
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
    LadderFilter filter;
    GMutex init_mutex;
    GCond init_cond;
    gboolean fully_initialized;
};

typedef struct WaveformGenerator WaveformGenerator;

// Function declarations
struct WaveformGenerator* waveform_generator_create(struct ParameterStore *params, 
                                                  struct ScopeWindow *scope, 
                                                  struct AudioManager *audio);
void waveform_generator_destroy(struct WaveformGenerator *gen);
void waveform_generator_set_audio_enabled(struct WaveformGenerator *gen, bool enable);

#endif // WAVEFORM_GENERATOR_H
