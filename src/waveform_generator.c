#include "waveform_generator.h"
#include "scope_window.h"
#include <math.h>

static size_t audio_callback(float *buffer, size_t frames, void *userdata);
static float generate_waveform(float phase, WaveformType type, float duty_cycle);
static gpointer generator_thread_func(gpointer data);
size_t circular_buffer_write(CircularBuffer *buffer, float *data, size_t frames);


static float generate_waveform(float phase, WaveformType type, float duty_cycle) {
    // Normalize phase to 0-2π range
    phase = fmodf(phase, 2.0f * M_PI);
    
    switch(type) {
        case WAVE_SINE:
            return sinf(phase);
            
        case WAVE_SQUARE: {
            float threshold = duty_cycle * 2.0f * M_PI;
            return (phase <= threshold) ? 1.0f : -1.0f;
        }
            
        case WAVE_SAW:
            return (phase / (2.0f * M_PI) * 2.0f) - 1.0f;
            
        case WAVE_TRIANGLE: {
            float normalized = phase / (2.0f * M_PI);
            if (normalized < 0.5f) {
                return normalized * 4.0f - 1.0f;
            } else {
                return 3.0f - normalized * 4.0f;
            }
        }
            
        default:
            return 0.0f;
    }
}

static size_t audio_callback(float *buffer, size_t frames, void *userdata) {
    WaveformGenerator *gen = (WaveformGenerator *)userdata;
    
    // Get current parameters with minimal lock time
    g_mutex_lock(&gen->params->mutex);
    WaveformType current_type = gen->params->waveform;
    float current_frequency = gen->params->frequency;
    float current_amplitude = gen->params->amplitude;
    float current_duty_cycle = gen->params->duty_cycle;
    float current_fm_freq = gen->params->fm_frequency;
    float current_fm_depth = gen->params->fm_depth;
    float current_am_freq = gen->params->am_frequency;
    float current_am_depth = gen->params->am_depth;
    float current_dcm_freq = gen->params->dcm_frequency;
    float current_dcm_depth = gen->params->dcm_depth;
    g_mutex_unlock(&gen->params->mutex);

    g_mutex_lock(&gen->mutex);
    float current_phase = gen->phase;
    float current_fm_phase = gen->fm_phase;
    float current_am_phase = gen->am_phase;
    float current_dcm_phase = gen->dcm_phase;
    
    // Generate samples
    for (size_t i = 0; i < frames; i++) {
        // Calculate FM modulation
        float frequency_mod = 0.0f;
        if (current_fm_freq > 0.0f) {
            frequency_mod = current_fm_depth * sinf(current_fm_phase);
            current_fm_phase = fmodf(current_fm_phase + 
                (2.0f * M_PI * current_fm_freq / SAMPLE_RATE), 2.0f * M_PI);
        }

        // Calculate duty cycle modulation
        float duty_mod = current_duty_cycle;
        if (current_dcm_freq > 0.0f) {
            duty_mod += current_dcm_depth * sinf(current_dcm_phase);
            duty_mod = fmaxf(0.1f, fminf(0.9f, duty_mod));
            current_dcm_phase = fmodf(current_dcm_phase + 
                (2.0f * M_PI * current_dcm_freq / SAMPLE_RATE), 2.0f * M_PI);
        }
        
        // Generate base waveform
        float wave_value = generate_waveform(current_phase, current_type, duty_mod);
        
        // Apply AM modulation
        float amplitude_mod = 1.0f;
        if (current_am_freq > 0.0f) {
            amplitude_mod = 1.0f + (current_am_depth * sinf(current_am_phase));
            current_am_phase = fmodf(current_am_phase + 
                (2.0f * M_PI * current_am_freq / SAMPLE_RATE), 2.0f * M_PI);
        }
        
        // Calculate final value
        float value = wave_value * current_amplitude * amplitude_mod * 0.25f;
        
        // Write to stereo buffer
        buffer[i * 2] = value;
        buffer[i * 2 + 1] = value;
        
        // Update phase
        current_phase = fmodf(current_phase + 
            (2.0f * M_PI * current_frequency / SAMPLE_RATE) * 
            (1.0f + frequency_mod), 2.0f * M_PI);
    }
    
    // Save phase states
    gen->phase = current_phase;
    gen->fm_phase = current_fm_phase;
    gen->am_phase = current_am_phase;
    gen->dcm_phase = current_dcm_phase;
    g_mutex_unlock(&gen->mutex);
    
    return frames;
}


#define TARGET_BUFFER_FILL (CIRCULAR_BUFFER_FRAMES / 2)  // Try to maintain 50% fill

// Modified generator thread sampling logic

static gpointer generator_thread_func(gpointer data) {
    WaveformGenerator *gen = (WaveformGenerator *)data;
    float audio_buffer[AUDIO_BUFFER_SIZE * 2];
    float scope_buffer[SCOPE_BUFFER_SIZE * 2];
    size_t scope_samples = 0;
    
    GTimer *timer = g_timer_new();
    gdouble last_scope_update = 0.0;
    
    while (TRUE) {
        g_mutex_lock(&gen->mutex);
        gboolean is_running = gen->running;
        g_mutex_unlock(&gen->mutex);
        
        if (!is_running) break;

        // Wait for audio callback timing
        if (gen->audio) {
            g_mutex_lock(&gen->audio->buffer.mutex);
            gboolean need_data = (gen->audio->buffer.frames_stored < AUDIO_BUFFER_SIZE * 2);
            if (!need_data) {
                g_cond_wait(&gen->audio->buffer.data_ready, &gen->audio->buffer.mutex);
            }
            g_mutex_unlock(&gen->audio->buffer.mutex);
        }
        
        // Generate audio
        size_t frames_written = audio_callback(audio_buffer, AUDIO_BUFFER_SIZE, gen);
        
        // Handle audio output
        if (gen->audio && audio_manager_is_playback_active(gen->audio)) {
            circular_buffer_write(&gen->audio->buffer, audio_buffer, frames_written);
        }
        
        // Handle scope updates independently of audio timing
        if (scope_samples + frames_written <= SCOPE_BUFFER_SIZE) {
            // Look for trigger point in new data before copying
            for (size_t i = 1; i < frames_written; i++) {
                float prev_sample = audio_buffer[(i-1)*2];  // Use left channel
                float curr_sample = audio_buffer[i*2];
                
                // Look for rising zero crossing
                if (prev_sample <= 0.0f && curr_sample > 0.0f) {
                    g_print("Trigger found at sample %zu (in frame)\n", i);
                }
            }

            memcpy(&scope_buffer[scope_samples * 2], audio_buffer, 
                   frames_written * sizeof(float) * 2);
            scope_samples += frames_written;
        }
        
        gdouble current_time = g_timer_elapsed(timer, NULL);
        if ((current_time - last_scope_update) * 1000.0 >= UPDATE_INTERVAL_MS) {
            if (scope_samples > 0) {
                scope_window_update_data(gen->scope, scope_buffer, scope_samples);
                scope_samples = 0;
                last_scope_update = current_time;
            }
        }
    }
    
    g_timer_destroy(timer);
    return NULL;
}


WaveformGenerator* waveform_generator_create(ParameterStore *params, ScopeWindow *scope, AudioManager *audio) {
    g_print("Creating waveform generator\n");
    
    WaveformGenerator *gen = g_new0(WaveformGenerator, 1);
    gen->params = params;
    gen->scope = scope;
    gen->audio = audio;
    gen->running = TRUE;
    gen->phase = 0.0f;
    gen->fm_phase = 0.0f;
    gen->am_phase = 0.0f;
    gen->dcm_phase = 0.0f;
    gen->sample_rate = SAMPLE_RATE;
    gen->buffer_size = BUFFER_SIZE;
    
    g_mutex_init(&gen->mutex);
    g_cond_init(&gen->cond);
    
    // Start generator thread
    gen->generator_thread = g_thread_new("waveform_generator", 
                                       generator_thread_func, gen);
    
    return gen;
}


void waveform_generator_destroy(WaveformGenerator *gen) {
    if (!gen) return;
    
    g_print("Destroying waveform generator\n");
    
    // Stop the generator thread safely
    g_mutex_lock(&gen->mutex);
    gen->running = FALSE;
    g_cond_signal(&gen->cond);
    g_mutex_unlock(&gen->mutex);
    
    // Disconnect audio
    if (gen->audio) {
        audio_manager_toggle_playback(gen->audio, false, NULL, NULL);
    }
    
    if (gen->generator_thread) {
        g_thread_join(gen->generator_thread);
        gen->generator_thread = NULL;
    }
    
    g_mutex_clear(&gen->mutex);
    g_cond_clear(&gen->cond);
    
    g_free(gen);
}

void waveform_generator_set_audio_enabled(WaveformGenerator *gen, bool enable) {
    if (!gen || !gen->audio) return;
    
    if (enable) {
        audio_manager_toggle_playback(gen->audio, true, audio_callback, gen);
    } else {
        audio_manager_toggle_playback(gen->audio, false, NULL, NULL);
    }
}

