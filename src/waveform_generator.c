#include "waveform_generator.h"
#include "parameter_store.h"
#include "scope_window.h"
#include "audio_manager.h"
#include <math.h>

static size_t audio_callback(float *buffer, size_t frames, void *userdata);
static float generate_waveform(float phase, WaveformType type, float duty_cycle);
static gpointer generator_thread_func(gpointer data);
size_t circular_buffer_write(CircularBuffer *buffer, float *data, size_t frames);

#define PINK_NOISE_OCTAVES 7
static float pink_noise_values[PINK_NOISE_OCTAVES] = {0};
static float pink_noise_total = 0;

static float generate_pink_noise(void) {
    float white = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
    
    // Update pink noise values
    pink_noise_total -= pink_noise_values[0];
    for (int i = 0; i < PINK_NOISE_OCTAVES - 1; i++) {
        pink_noise_values[i] = pink_noise_values[i + 1];
    }
    pink_noise_values[PINK_NOISE_OCTAVES - 1] = white;
    pink_noise_total += white;
    
    // Mix white noise with filtered pink noise
    float pink = pink_noise_total / PINK_NOISE_OCTAVES;
    return (pink + white) * 0.5f;
}


// Utility function for tanh approximation (faster than std tanh)
static float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static void ladder_filter_reset(LadderFilter *filter) {
    memset(filter->stage, 0, sizeof(float) * FILTER_STAGES);
    memset(filter->delay, 0, sizeof(float) * FILTER_STAGES);
    filter->cutoff_mod = 0.0f;  // Reset modulation
    filter->res_mod = 0.0f;     // Reset resonance mod
}

static float ladder_filter_process(LadderFilter *filter, float input, float sample_rate) {
    // Calculate and bound cutoff frequency with extra safety margin
    float fc = filter->cutoff + filter->cutoff_mod;
    fc = fmaxf(20.0f, fminf(fc, 20000.0f));
    
    // Add extra safety bound for very low frequencies
    fc = fmaxf(fc, sample_rate * 0.0005f);  // Minimum 0.05% of sample rate
    
    // Smoothed frequency normalization
    float f = fminf(0.499f, fc / sample_rate);  // Prevent getting too close to Nyquist

    // Enhanced resonance response
    float res = filter->resonance + filter->res_mod;
    res = fmaxf(0.0f, fminf(res, 1.0f));
    
    // Scale resonance for feedback (reduced from 4.0 to 3.8 to prevent self-oscillation getting too extreme)
    float scaled_res = 3.8f * powf(res, 0.5f);
    
    // Compute filter coefficients
    float k = 4.0f * (f * M_PI);
    float p = k / (1.0f + k);
    
    // Adjusted compensation - only compensate for resonance-induced gain changes
    float comp = 1.0f / (1.0f + scaled_res * 0.1f);
    
    // Input with resonance feedback
    float input_with_res = input - scaled_res * filter->delay[3];
    input_with_res *= comp;
    
    // Cascade of 4 one-pole filters
    float stages[4];
    stages[0] = input_with_res;
    
    for (int i = 0; i < 4; i++) {
        if (i > 0) stages[i] = filter->delay[i-1];
        
        // Nonlinear processing - scale back the resonance influence
        stages[i] = fast_tanh(stages[i] * (1.0f + 0.3f * res));
        
        // One-pole lowpass filter
        filter->delay[i] = filter->delay[i] + p * (stages[i] - filter->delay[i]);
    }
    
    return filter->delay[3];
}

static float generate_waveform(float phase, WaveformType type, float duty_cycle) {
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

        case WAVE_PINK_NOISE:
            return generate_pink_noise();
            
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
    float current_cutoff = gen->params->filter_cutoff;
    float current_resonance = gen->params->filter_resonance;
    float current_cutoff_lfo_freq = gen->params->filter_cutoff_lfo_freq;
    float current_cutoff_lfo_amount = gen->params->filter_cutoff_lfo_amount;
    float current_res_lfo_freq = gen->params->filter_res_lfo_freq;
    float current_res_lfo_amount = gen->params->filter_res_lfo_amount;
    g_mutex_unlock(&gen->params->mutex);

    // Get phase variables with minimal lock time
    g_mutex_lock(&gen->mutex);
    float current_phase = gen->phase;
    float current_fm_phase = gen->fm_phase;
    float current_am_phase = gen->am_phase;
    float current_dcm_phase = gen->dcm_phase;
    float cutoff_lfo_phase = gen->filter.cutoff_lfo_phase;
    float res_lfo_phase = gen->filter.res_lfo_phase;
    g_mutex_unlock(&gen->mutex);

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

        // Calculate filter modulation - use Hz range for cutoff modulation
        float cutoff_mod = 0.0f;
        if (current_cutoff_lfo_freq > 0.0f) {
            // Modulate between 20Hz and current cutoff frequency
            float mod_range = current_cutoff - 20.0f;
            cutoff_mod = current_cutoff_lfo_amount * sinf(cutoff_lfo_phase) * mod_range;
            cutoff_lfo_phase += 2.0f * M_PI * current_cutoff_lfo_freq / SAMPLE_RATE;
            if (cutoff_lfo_phase >= 2.0f * M_PI)
                cutoff_lfo_phase -= 2.0f * M_PI;
        }

        float res_mod = 0.0f;
        if (current_res_lfo_freq > 0.0f) {
            res_mod = current_res_lfo_amount * sinf(res_lfo_phase);
            res_lfo_phase += 2.0f * M_PI * current_res_lfo_freq / SAMPLE_RATE;
            if (res_lfo_phase >= 2.0f * M_PI)
                res_lfo_phase -= 2.0f * M_PI;
        }

        // Apply filter
        gen->filter.cutoff = current_cutoff;
        gen->filter.resonance = current_resonance;
        gen->filter.cutoff_mod = cutoff_mod;
        gen->filter.res_mod = res_mod;

        wave_value = ladder_filter_process(&gen->filter, wave_value, SAMPLE_RATE);

        // Apply AM modulation
        float amplitude_mod = 1.0f;
        if (current_am_freq > 0.0f) {
            amplitude_mod = 1.0f + (current_am_depth * sinf(current_am_phase));
            current_am_phase = fmodf(current_am_phase +
                (2.0f * M_PI * current_am_freq / SAMPLE_RATE), 2.0f * M_PI);
        }

        // Calculate final value
        float value = wave_value * current_amplitude * amplitude_mod;

        // Write to stereo buffer
        buffer[i * 2] = value;
        buffer[i * 2 + 1] = value;

        // Update phase
        current_phase = fmodf(current_phase +
            (2.0f * M_PI * current_frequency / SAMPLE_RATE) *
            (1.0f + frequency_mod), 2.0f * M_PI);
    }

    // Save phase states with minimal lock time
    g_mutex_lock(&gen->mutex);
    gen->phase = current_phase;
    gen->fm_phase = current_fm_phase;
    gen->am_phase = current_am_phase;
    gen->dcm_phase = current_dcm_phase;
    gen->filter.cutoff_lfo_phase = cutoff_lfo_phase;
    gen->filter.res_lfo_phase = res_lfo_phase;
    g_mutex_unlock(&gen->mutex);

    return frames;
}

//#define TARGET_BUFFER_FILL (CIRCULAR_BUFFER_FRAMES / 2)  // Try to maintain 50% fill
static gpointer generator_thread_func(gpointer data) {
    g_print("Generator thread: Starting initialization\n");
    
    if (!data) {
        g_print("ERROR: Generator thread started with NULL data\n");
        return NULL;
    }

    struct WaveformGenerator *gen = (struct WaveformGenerator *)data;
    g_print("Generator thread: Got generator pointer\n");
    
    if (!gen->scope) {
        g_print("ERROR: Generator has NULL scope\n");
        return NULL;
    }
    g_print("Generator thread: Got valid scope pointer\n");
    
    if (!gen->scope->waveform_data) {
        g_print("ERROR: Scope has NULL waveform data\n");
        return NULL;
    }
    g_print("Generator thread: Got valid waveform data\n");
    
    // Initialize FFT if needed
    if (gen->scope->fft) {
        g_print("Generator thread: Found FFT analyzer\n");
    }

    float *audio_buffer = g_malloc(AUDIO_BUFFER_SIZE * 2 * sizeof(float));
    float *scope_buffer = g_malloc(SCOPE_BUFFER_SIZE * 2 * sizeof(float));
    size_t scope_samples = 0;
    
    if (!audio_buffer || !scope_buffer) {
        g_print("ERROR: Failed to allocate generator buffers\n");
        g_free(audio_buffer);
        g_free(scope_buffer);
        return NULL;
    }
   
    g_print("Generator thread: Local buffers initialized\n");
    size_t failed_lock_count = 0;
    bool was_locked_out = false;
    
    while (TRUE) {
        if (!gen->scope) {  // Check again in loop
            g_print("ERROR: Scope became NULL during operation\n");
            break;
        }

        g_mutex_lock(&gen->mutex);
        gboolean is_running = gen->running;
        g_mutex_unlock(&gen->mutex);
        
        if (!is_running) {
            g_print("Generator thread stopping normally\n");
            break;
        }

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
        
        // Always accumulate in local buffer
        if (scope_samples + frames_written <= SCOPE_BUFFER_SIZE) {
            memcpy(&scope_buffer[scope_samples * 2], audio_buffer, 
                   frames_written * sizeof(float) * 2);
            scope_samples += frames_written;
        } else {
            // Buffer is full, shift data left and add new samples at end
            size_t remaining = SCOPE_BUFFER_SIZE - frames_written;
            if (remaining > 0) {
                memmove(scope_buffer, &scope_buffer[frames_written * 2], 
                       remaining * sizeof(float) * 2);
            }
            memcpy(&scope_buffer[remaining * 2], audio_buffer,
                   frames_written * sizeof(float) * 2);
            scope_samples = SCOPE_BUFFER_SIZE;
        }

        // Try to update display buffer - but keep accumulating even if we can't
        if (g_mutex_trylock(&gen->scope->update_mutex)) {
            if (g_mutex_trylock(&gen->scope->data_mutex)) {
                if (was_locked_out) {
                    g_print("Display update resumed after %zu failed attempts\n", failed_lock_count);
                    was_locked_out = false;
                    failed_lock_count = 0;
                }
                
                if (gen->scope->waveform_data && scope_samples > 0) {
                    size_t bytes_to_copy = scope_samples * sizeof(float) * 2;
                    size_t max_bytes = gen->scope->data_size * sizeof(float) * 2;
                    
                    if (bytes_to_copy <= max_bytes) {
                        memcpy(gen->scope->waveform_data, scope_buffer, bytes_to_copy);
                        gen->scope->write_pos = scope_samples;
                        
                        if (gen->scope->drawing_area && GTK_IS_WIDGET(gen->scope->drawing_area)) {
                            gtk_widget_queue_draw(gen->scope->drawing_area);
                        }
                    }
                }
                g_mutex_unlock(&gen->scope->data_mutex);
            }
            g_mutex_unlock(&gen->scope->update_mutex);
        } else {
            if (!was_locked_out) {
                g_print("Display update locked out\n");
                was_locked_out = true;
            }
            failed_lock_count++;
            if (failed_lock_count % 1000 == 0) {  // Log every 1000 failures
                g_print("Still locked out after %zu attempts\n", failed_lock_count);
            }
        }
    }
    
    g_free(audio_buffer);
    g_free(scope_buffer);
    g_print("Generator thread exiting\n");
    return NULL;
}

struct WaveformGenerator* waveform_generator_create(ParameterStore *params, struct ScopeWindow *scope, AudioManager *audio) {
    g_print("Creating waveform generator\n");
    
    WaveformGenerator *gen = g_new0(WaveformGenerator, 1);
    gen->params = params;
    gen->scope = scope;
    gen->audio = audio;
    gen->running = FALSE;  // Start as not running
    gen->phase = 0.0f;
    gen->fm_phase = 0.0f;
    gen->am_phase = 0.0f;
    gen->dcm_phase = 0.0f;
    gen->sample_rate = SAMPLE_RATE;
    gen->buffer_size = BUFFER_SIZE;
    
    // Initialize filter
    ladder_filter_reset(&gen->filter);
    
    g_mutex_init(&gen->mutex);
    g_cond_init(&gen->cond);
    
    // Don't start thread yet
    gen->generator_thread = NULL;
    
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


void waveform_generator_start(WaveformGenerator *gen) {
    if (!gen || gen->generator_thread) return;  // Already running
    
    g_mutex_lock(&gen->mutex);
    gen->running = TRUE;
    gen->generator_thread = g_thread_new("waveform_generator", 
                                       generator_thread_func, gen);
    g_mutex_unlock(&gen->mutex);
}
