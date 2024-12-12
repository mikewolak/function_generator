#include "parameter_store.h"
#include <stdlib.h>

ParameterStore* parameter_store_create(void) {
    ParameterStore *store = g_new0(ParameterStore, 1);
    
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
    
    return store;
}

void parameter_store_destroy(ParameterStore *store) {
    if (!store) return;
    
    g_mutex_clear(&store->mutex);
    g_cond_clear(&store->changed);
    g_free(store);
}

void parameter_store_set_waveform(ParameterStore *store, WaveformType type) {
    g_mutex_lock(&store->mutex);
    g_print("Setting waveform type: %d\n", type);
    store->waveform = type;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_frequency(ParameterStore *store, float freq) {
    g_mutex_lock(&store->mutex);
    g_print("Setting frequency: %.2f Hz\n", freq);
    store->frequency = freq;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_amplitude(ParameterStore *store, float amp) {
    g_mutex_lock(&store->mutex);
    g_print("Setting amplitude: %.2f\n", amp);
    store->amplitude = amp;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_duty_cycle(ParameterStore *store, float duty) {
    g_mutex_lock(&store->mutex);
    g_print("Setting duty cycle: %.2f%%\n", duty * 100.0f);
    store->duty_cycle = duty;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_fm(ParameterStore *store, float freq, float depth) {
    g_mutex_lock(&store->mutex);
    g_print("Setting FM: freq=%.2f Hz, depth=%.2f\n", freq, depth);
    store->fm_frequency = freq;
    store->fm_depth = depth;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_am(ParameterStore *store, float freq, float depth) {
    g_mutex_lock(&store->mutex);
    g_print("Setting AM: freq=%.2f Hz, depth=%.2f\n", freq, depth);
    store->am_frequency = freq;
    store->am_depth = depth;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_preview_mode(ParameterStore *store, gboolean local) {
    g_mutex_lock(&store->mutex);
    g_print("Setting preview mode: %s\n", local ? "local" : "remote");
    store->local_preview = local;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_adc_mode(ParameterStore *store, gboolean use_adc) {
    g_mutex_lock(&store->mutex);
    g_print("Setting ADC mode: %s\n", use_adc ? "enabled" : "disabled");
    store->use_adc = use_adc;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}

void parameter_store_set_dcm(ParameterStore *store, float freq, float depth) {
    g_mutex_lock(&store->mutex);
    g_print("Setting DCM: freq=%.2f Hz, depth=%.2f\n", freq, depth);
    store->dcm_frequency = freq;
    store->dcm_depth = depth;
    g_cond_signal(&store->changed);
    g_mutex_unlock(&store->mutex);
}
