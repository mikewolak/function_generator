// parameter_store.h
#ifndef PARAMETER_STORE_H
#define PARAMETER_STORE_H

#include <gtk/gtk.h>

typedef enum {
    WAVE_SINE,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_TRIANGLE
} WaveformType;

// parameter_store.h
typedef struct {
    WaveformType waveform;
    float frequency;
    float amplitude;
    float duty_cycle;
    float fm_frequency;
    float fm_depth;
    float am_frequency;
    float am_depth;
    float dcm_frequency;    // Duty Cycle Modulation frequency
    float dcm_depth;        // Duty Cycle Modulation depth
    gboolean local_preview;
    gboolean use_adc;
    GMutex mutex;
    GCond changed;
} ParameterStore;

// Add new function declaration
void parameter_store_set_dcm(ParameterStore *store, float freq, float depth);

// Create and initialize parameter store
ParameterStore* parameter_store_create(void);

// Clean up resources
void parameter_store_destroy(ParameterStore *store);

// Thread-safe parameter updates
void parameter_store_set_waveform(ParameterStore *store, WaveformType type);
void parameter_store_set_frequency(ParameterStore *store, float freq);
void parameter_store_set_amplitude(ParameterStore *store, float amp);
void parameter_store_set_duty_cycle(ParameterStore *store, float duty);
void parameter_store_set_fm(ParameterStore *store, float freq, float depth);
void parameter_store_set_am(ParameterStore *store, float freq, float depth);
void parameter_store_set_preview_mode(ParameterStore *store, gboolean local);
void parameter_store_set_adc_mode(ParameterStore *store, gboolean use_adc);
void parameter_store_set_dcm(ParameterStore *store, float freq, float depth);


#endif // PARAMETER_STORE_H
