#include "parameter_store.h"
#include <stdlib.h>
#include <math.h>

struct ParameterStore* parameter_store_create(void) {
    struct ParameterStore *store = g_new0(struct ParameterStore, 1);
    
    g_mutex_init(&store->mutex);
    g_cond_init(&store->changed);
    
    store->waveform = WAVE_SINE;
    store->frequency = 440.0f;
    store->amplitude = 1.0f;
    store->duty_cycle = 0.5f;
    store->fm_frequency = 0.0f;
    store->fm_depth = 0.0f;
    store->am_frequency = 0.0f;
    store->am_depth = 0.0f;
    store->local_preview = TRUE;
    store->use_adc = FALSE;
    
    // Initialize filter parameters
    store->filter_cutoff = 20000.0f;  // Start fully open
    store->filter_resonance = 0.0f;   // Start with no resonance
    store->filter_cutoff_lfo_freq = 0.0f;
    store->filter_cutoff_lfo_amount = 0.0f;
    store->filter_res_lfo_freq = 0.0f;
    store->filter_res_lfo_amount = 0.0f;
    
    return store;
}

void parameter_store_destroy(struct ParameterStore *store) {
    if (!store) return;
    
    g_mutex_clear(&store->mutex);
    g_cond_clear(&store->changed);
    g_free(store);
}

void parameter_store_set_waveform(struct ParameterStore *store, WaveformType type) {
    g_mutex_lock(&store->mutex);
    g_print("Setting waveform type: %d\n", type);
    store->waveform = type;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_frequency(struct ParameterStore *store, float freq) {
    g_mutex_lock(&store->mutex);
    g_print("Setting frequency: %.2f Hz\n", freq);
    store->frequency = freq;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_amplitude(struct ParameterStore *store, float amp) {
    g_mutex_lock(&store->mutex);
    g_print("Setting amplitude: %.2f\n", amp);
    store->amplitude = amp;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_duty_cycle(struct ParameterStore *store, float duty) {
    g_mutex_lock(&store->mutex);
    g_print("Setting duty cycle: %.2f%%\n", duty * 100.0f);
    store->duty_cycle = duty;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_fm(struct ParameterStore *store, float freq, float depth) {
    g_mutex_lock(&store->mutex);
    g_print("Setting FM: freq=%.2f Hz, depth=%.2f\n", freq, depth);
    store->fm_frequency = freq;
    store->fm_depth = depth;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_am(struct ParameterStore *store, float freq, float depth) {
    g_mutex_lock(&store->mutex);
    g_print("Setting AM: freq=%.2f Hz, depth=%.2f\n", freq, depth);
    store->am_frequency = freq;
    store->am_depth = depth;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_preview_mode(struct ParameterStore *store, gboolean local) {
    g_mutex_lock(&store->mutex);
    g_print("Setting preview mode: %s\n", local ? "local" : "remote");
    store->local_preview = local;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_adc_mode(struct ParameterStore *store, gboolean use_adc) {
    g_mutex_lock(&store->mutex);
    g_print("Setting ADC mode: %s\n", use_adc ? "enabled" : "disabled");
    store->use_adc = use_adc;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_dcm(struct ParameterStore *store, float freq, float depth) {
    g_mutex_lock(&store->mutex);
    g_print("Setting DCM: freq=%.2f Hz, depth=%.2f\n", freq, depth);
    store->dcm_frequency = freq;
    store->dcm_depth = depth;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_filter_cutoff(struct ParameterStore *store, float cutoff) {
    g_mutex_lock(&store->mutex);
    g_print("Setting filter cutoff: %.2f Hz\n", cutoff);
    store->filter_cutoff = cutoff;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_filter_resonance(struct ParameterStore *store, float resonance) {
    g_mutex_lock(&store->mutex);
    g_print("Setting filter resonance: %.2f\n", resonance);
    store->filter_resonance = resonance;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_filter_cutoff_lfo(struct ParameterStore *store, float freq, float amount) {
    g_mutex_lock(&store->mutex);
    g_print("Setting cutoff LFO: freq=%.2f Hz, amount=%.2f\n", freq, amount);
    store->filter_cutoff_lfo_freq = freq;
    store->filter_cutoff_lfo_amount = amount;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_filter_res_lfo(struct ParameterStore *store, float freq, float amount) {
    g_mutex_lock(&store->mutex);
    g_print("Setting resonance LFO: freq=%.2f Hz, amount=%.2f\n", freq, amount);
    store->filter_res_lfo_freq = freq;
    store->filter_res_lfo_amount = amount;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}
