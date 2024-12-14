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
   if (!data) {
       g_print("ERROR: Generator thread started with NULL data\n");
       return NULL;
   }

   WaveformGenerator *gen = (WaveformGenerator *)data;
   if (!gen->scope) {
       g_print("ERROR: Generator has NULL scope\n");
       return NULL;
   }
   
   g_print("Generator thread starting\n");
   float audio_buffer[AUDIO_BUFFER_SIZE * 2];
   float scope_buffer[SCOPE_BUFFER_SIZE * 2];
   size_t scope_samples = 0;
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
                       
                       static gint64 last_draw_time = 0;
                       gint64 current_time = g_get_monotonic_time();
                       
                       // Limit redraws to TARGET_FPS
                       if (current_time - last_draw_time > FRAME_TIME_US) {
                           if (gen->scope->drawing_area && GTK_IS_WIDGET(gen->scope->drawing_area)) {
                               GtkAllocation allocation;
                               gtk_widget_get_allocation(gen->scope->drawing_area, &allocation);
                               
                               // Only queue draw if widget has valid size
                               if (allocation.width > 0 && allocation.height > 0) {
                                   g_print("Queueing redraw after %lld microseconds, widget size: %dx%d\n",
                                           current_time - last_draw_time,
                                           allocation.width, allocation.height);
                                   gtk_widget_queue_draw(gen->scope->drawing_area);
                                   last_draw_time = current_time;
                               }
                           }
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
           g_usleep(100);
       }
   }
   
   g_print("Generator thread exiting\n");
   return NULL;
}

WaveformGenerator* waveform_generator_create(ParameterStore *params, ScopeWindow *scope, AudioManager *audio) {
    g_print("Creating waveform generator\n");
    
    if (!params || !scope) {
        g_print("Error: Invalid parameters for waveform generator\n");
        return NULL;
    }

    WaveformGenerator *gen = g_new0(WaveformGenerator, 1);
    if (!gen) {
        g_print("Error: Failed to allocate waveform generator\n");
        return NULL;
    }
    
    // Initialize basic parameters
    gen->params = params;
    gen->scope = scope;
    gen->audio = audio;  // Can be NULL
    gen->running = FALSE;  // Don't start thread until fully initialized
    gen->phase = 0.0f;
    gen->fm_phase = 0.0f;
    gen->am_phase = 0.0f;
    gen->dcm_phase = 0.0f;
    gen->sample_rate = SAMPLE_RATE;
    gen->buffer_size = BUFFER_SIZE;
    
    // Initialize synchronization primitives
    g_mutex_init(&gen->mutex);
    g_cond_init(&gen->cond);
    
    // Now that everything is initialized, mark as running and start thread
    gen->running = TRUE;
    
    g_print("Starting generator thread...\n");
    gen->generator_thread = g_thread_new("waveform_generator", 
                                       generator_thread_func, gen);
    
    if (!gen->generator_thread) {
        g_print("Error: Failed to create generator thread\n");
        g_mutex_clear(&gen->mutex);
        g_cond_clear(&gen->cond);
        g_free(gen);
        return NULL;
    }
    
    g_print("Waveform generator created successfully\n");
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

