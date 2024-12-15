#ifndef PARAMETER_STORE_H
#define PARAMETER_STORE_H

#include <glib.h>

typedef enum {
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_TRIANGLE
} WaveformType;

struct ParameterStore {
    GMutex mutex;
    GCond changed;
    
    // Waveform parameters
    WaveformType waveform;
    float frequency;
    float amplitude;
    float duty_cycle;
    float fm_frequency;
    float fm_depth;
    float am_frequency;
    float am_depth;
    float dcm_frequency;
    float dcm_depth;
    gboolean local_preview;
    gboolean use_adc;
    
    // Filter parameters
    float filter_cutoff;
    float filter_resonance;
    float filter_cutoff_lfo_freq;
    float filter_cutoff_lfo_amount;
    float filter_res_lfo_freq;
    float filter_res_lfo_amount;
};

typedef struct ParameterStore ParameterStore;

// Function declarations
struct ParameterStore* parameter_store_create(void);
void parameter_store_destroy(struct ParameterStore *store);
void parameter_store_set_waveform(struct ParameterStore *store, WaveformType type);
void parameter_store_set_frequency(struct ParameterStore *store, float freq);
void parameter_store_set_amplitude(struct ParameterStore *store, float amp);
void parameter_store_set_duty_cycle(struct ParameterStore *store, float duty);
void parameter_store_set_fm(struct ParameterStore *store, float freq, float depth);
void parameter_store_set_am(struct ParameterStore *store, float freq, float depth);
void parameter_store_set_preview_mode(struct ParameterStore *store, gboolean local);
void parameter_store_set_adc_mode(struct ParameterStore *store, gboolean use_adc);
void parameter_store_set_dcm(struct ParameterStore *store, float freq, float depth);

// New filter functions
void parameter_store_set_filter_cutoff(struct ParameterStore *store, float cutoff);
void parameter_store_set_filter_resonance(struct ParameterStore *store, float resonance);
void parameter_store_set_filter_cutoff_lfo(struct ParameterStore *store, float freq, float amount);
void parameter_store_set_filter_res_lfo(struct ParameterStore *store, float freq, float amount);

#endif // PARAMETER_STORE_H
