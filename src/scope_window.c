#include "scope_window.h"  // Must be first
#include <math.h>
#include "common_defs.h"

static gboolean find_trigger_point(const float *buffer, size_t buffer_size, 
                                 size_t display_width, struct TriggerInfo *trigger) {
    if (!buffer || buffer_size < 4 || !trigger) {
        return FALSE;
    }
    
    // Ensure we have enough samples for pre/post trigger
    size_t safe_size = MIN(buffer_size, SCOPE_BUFFER_SIZE);
    size_t pre_trigger_samples = display_width / 3;
    size_t post_trigger_samples = display_width - pre_trigger_samples;
    
    // Ensure search range is valid
    size_t search_start = 2;  // Need 2 samples before for slope
    size_t search_end = safe_size - 2;  // Need 2 samples after
    
    if (search_end <= search_start) {
        return FALSE;
    }

    // Add RMS calculation to check signal level
    float rms = 0.0f;
    for (size_t i = 0; i < safe_size; i++) {
        float sample = buffer[i * 2];
        rms += sample * sample;
    }
    rms = sqrtf(rms / safe_size);

    // If signal is too low, use center of buffer
    if (rms < 0.01f) {  // Threshold for "too low"
        trigger->position = safe_size / 2;
        trigger->value = buffer[trigger->position * 2];
        trigger->valid = TRUE;
        return TRUE;
    }
            
    // Normal trigger search for sufficient signal
    for (size_t i = search_start; i < search_end; i++) {
        size_t prev_idx = (i-1) * 2;
        size_t curr_idx = i * 2;
        
        if (curr_idx >= safe_size * 2) break;
        
        float prev_sample = buffer[prev_idx];
        float curr_sample = buffer[curr_idx];
        
        if (prev_sample <= 0.0f && curr_sample > 0.0f) {
            // Only use trigger point if we have enough samples after it
            if (i + post_trigger_samples < safe_size) {
                trigger->position = i;
                trigger->value = curr_sample;
                trigger->valid = TRUE;
                return TRUE;
            }
        }
    }
    
    // If no trigger found, use center of buffer
    trigger->position = safe_size / 2;
    trigger->value = buffer[trigger->position * 2];
    trigger->valid = TRUE;
    return TRUE;
}

static void on_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
    (void)widget;  // Mark as intentionally unused
    struct ScopeWindow *scope = (struct ScopeWindow *)data;
    if (!scope) return;
    
    // Try to get both locks or don't update at all
    if (g_mutex_trylock(&scope->update_mutex)) {
        if (g_mutex_trylock(&scope->data_mutex)) {
            if (scope->window_width != allocation->width || 
                scope->window_height != allocation->height) {
                scope->window_width = allocation->width;
                scope->window_height = allocation->height;
                scope->size_changed = TRUE;
                g_print("Scope window resized to: %dx%d\n", 
                        scope->window_width, scope->window_height);
            }
            g_mutex_unlock(&scope->data_mutex);
        }
        g_mutex_unlock(&scope->update_mutex);
    }
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr) {
    g_print("Starting scope draw...\n");
    
    struct ScopeWindow *scope = (struct ScopeWindow *)g_object_get_data(G_OBJECT(widget), "scope");
    if (!scope) {
        g_print("Error: No scope data found for widget\n");
        return FALSE;
    }

    scope->drawing_in_progress = TRUE;  // Add this line early
    
    // Get widget dimensions first
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    g_print("Widget dimensions: %d x %d\n", allocation.width, allocation.height);
    int width = allocation.width;
    int height = allocation.height;
    if (width <= 0 || height <= 0) {
        return FALSE;
    }
    
    g_print("Calculating heights...\n");
    // Calculate split heights
    int wave_height = (height * 2) / 3;
    int fft_height = height - wave_height;
    scope->fft_height = fft_height;

    g_print("Drawing background...\n");
    // Draw black background - this is safe without data
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);

    // Allocate local data buffer only if we'll use it
    float *local_data = NULL;
    size_t local_write_pos = 0;
    gboolean have_data = FALSE;

    g_print("Checking scope data size: %zu\n", scope->data_size);
    if (scope->data_size > 0) {
        g_print("Allocating local buffer of size %zu\n", scope->data_size * 2 * sizeof(float));
        local_data = g_malloc(scope->data_size * 2 * sizeof(float));
        if (local_data) {
            g_print("Local buffer allocated at %p\n", (void*)local_data);
            if (g_mutex_trylock(&scope->data_mutex)) {
                g_print("Data mutex locked, write_pos: %zu\n", scope->write_pos);
                local_write_pos = scope->write_pos;
                if (local_write_pos > 0 && scope->waveform_data) {
                    g_print("Copying %zu bytes from waveform_data\n", local_write_pos * 2 * sizeof(float));
                    memcpy(local_data, scope->waveform_data, local_write_pos * 2 * sizeof(float));
                    have_data = TRUE;
                }
                g_mutex_unlock(&scope->data_mutex);
                g_print("Data mutex unlocked\n");
            }
        } else {
            g_print("Failed to allocate local buffer\n");
        }
    }

    g_print("Drawing waveform grid...\n");
    // Draw waveform grid
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_set_line_width(cr, 1.0);
    
    // Vertical divisions for waveform
    float div_width = width / 12.0f;
    for (int i = 0; i <= 12; i++) {
        double x = i * div_width;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, wave_height);
    }
    
    // Horizontal divisions for waveform
    float div_height = wave_height / 8.0f;
    for (int i = 0; i <= 8; i++) {
        double y = i * div_height;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, width, y);
    }
    cairo_stroke(cr);

    // Draw dividing line between waveform and FFT
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 0, wave_height);
    cairo_line_to(cr, width, wave_height);
    cairo_stroke(cr);
    
    g_print("Processing waveform data...\n");
    // Draw waveform if we have data
    if (have_data && local_write_pos > 0) {
        float *display_data = g_malloc(width * sizeof(float));
        if (display_data) {
            g_print("Display buffer allocated at %p\n", (void*)display_data);
            scope->trigger.valid = FALSE;  // Force new trigger search
            find_trigger_point(local_data, local_write_pos, width, &scope->trigger);
            
            if (scope->trigger.valid) {
                scope_window_downsample_buffer(local_data, local_write_pos,
                                            display_data, width,
                                            scope->trigger.position);
                
                // Draw waveform
                cairo_set_source_rgb(cr, 0, 1, 0);
                cairo_set_line_width(cr, 2.0);
                
                float half_height = wave_height / 2.0f;
                float scale = wave_height / 4.0f;
                
                cairo_move_to(cr, 0, half_height - display_data[0] * scale);
                for (int x = 1; x < width; x++) {
                    cairo_line_to(cr, x, half_height - display_data[x] * scale);
                }
                cairo_stroke(cr);
                
                // Draw trigger marker
                cairo_set_source_rgb(cr, 1, 0, 0);
                cairo_set_line_width(cr, 1.0);
                int trigger_x = width/3;
                cairo_move_to(cr, trigger_x, 0);
                cairo_line_to(cr, trigger_x, wave_height);
                cairo_stroke(cr);
            }
            
            g_print("Freeing display buffer %p\n", (void*)display_data);
            g_free(display_data);
        }
    }
 g_print("Processing FFT data...\n");
    // Draw FFT if enabled
    if (have_data && scope->show_fft && scope->fft && local_write_pos > 0) {
        // Process current buffer through FFT
        fft_analyzer_process(scope->fft, local_data, local_write_pos);
        
        // Draw FFT grid
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_set_line_width(cr, 1.0);
        
        // Draw frequency grid lines
        double freq_markers[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
        int num_markers = sizeof(freq_markers) / sizeof(freq_markers[0]);
        
        for (int i = 0; i < num_markers; i++) {
            double freq = freq_markers[i];
            double log_pos = log(freq/20.0) / log(SAMPLE_RATE/2 / 20.0);
            double x = width * log_pos;
            
            cairo_move_to(cr, x, wave_height);
            cairo_line_to(cr, x, height);
        }
        cairo_stroke(cr);
        
        // Draw amplitude grid lines
        for (int db = -80; db <= 0; db += 20) {
            float y = wave_height + fft_height * (1.0 - (float)(db - MIN_DB) / (MAX_DB - MIN_DB));
            cairo_move_to(cr, 0, y);
            cairo_line_to(cr, width, y);
        }
        cairo_stroke(cr);

        // Draw FFT spectrum
        cairo_set_source_rgba(cr, 1, 1, 0, 0.8);
        cairo_set_line_width(cr, 1.5);
        
        gboolean first_point = TRUE;
        float max_magnitude = 0.0f;
        float peak_x = 0;
        float peak_y = 0;
        float peak_freq = 0;
        
        for (int x = 0; x < width; x++) {
            // Map screen position to frequency linearly in log space
            double log_pos = (double)x / width;
            double freq = 20.0 * exp(log_pos * log(SAMPLE_RATE/2 / 20.0));
            freq = fmin(freq, SAMPLE_RATE/2);
            
            // Direct bin calculation
            size_t bin = (size_t)((freq * FFT_SIZE) / SAMPLE_RATE);
            bin = MIN(bin, FFT_SIZE/2);
            
            if (bin < FFT_SIZE/2) {
                float magnitude = scope->fft->magnitudes[bin];
                float y = wave_height + fft_height * (1.0f - magnitude);
                y = fminf(fmaxf(y, wave_height), height);
                
                // Track maximum magnitude
                if (magnitude > max_magnitude) {
                    max_magnitude = magnitude;
                    peak_x = x;
                    peak_y = y;
                    peak_freq = freq;
                }
                
                if (first_point) {
                    cairo_move_to(cr, x, y);
                    first_point = FALSE;
                } else {
                    cairo_line_to(cr, x, y);
                }
            }
        }
        cairo_stroke(cr);

        // Draw labels
        cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
        cairo_set_font_size(cr, 10);
        
        // Frequency labels
        for (int i = 0; i < num_markers; i++) {
            double freq = freq_markers[i];
            double log_pos = log(freq/20.0) / log(SAMPLE_RATE/2 / 20.0);
            double x = width * log_pos;
            
            char freq_label[32];
            if (freq >= 1000) {
                snprintf(freq_label, sizeof(freq_label), "%.1fk", freq/1000.0);
            } else {
                snprintf(freq_label, sizeof(freq_label), "%.0f", freq);
            }
            
            if (freq == 20 || freq == 100 || freq == 1000 || freq == 10000 || freq == 20000) {
                cairo_move_to(cr, x - 10, height - 5);
                cairo_show_text(cr, freq_label);
            }
        }
        
        // dB labels
        for (int db = -80; db <= 0; db += 20) {
            float y = wave_height + fft_height * (1.0 - (float)(db - MIN_DB) / (MAX_DB - MIN_DB));
            char db_label[32];
            snprintf(db_label, sizeof(db_label), "%ddB", db);
            cairo_move_to(cr, 5, y - 2);
            cairo_show_text(cr, db_label);
        }

        // Draw peak frequency label if we found a peak
        if (max_magnitude > 0.01f) {  // Lower threshold to catch smaller peaks
            cairo_set_source_rgb(cr, 1, 1, 1);  // White text
            cairo_set_font_size(cr, 12);
            
            char freq_label[32];
            if (peak_freq >= 1000) {
                snprintf(freq_label, sizeof(freq_label), "%.1f kHz", peak_freq/1000.0);
            } else {
                snprintf(freq_label, sizeof(freq_label), "%.1f Hz", peak_freq);
            }
            
            // Get text extents for centering
            cairo_text_extents_t extents;
            cairo_text_extents(cr, freq_label, &extents);
            
            // Position label 5 pixels above peak
            double label_x = peak_x - (extents.width / 2);
            double label_y = peak_y - 15;  // Move label further up for better visibility
            
            // Adjusted bounds checking - ensure we don't clip at wave_height
            label_x = fmax(5, fmin(label_x, width - extents.width - 5));
            label_y = fmin(label_y, height - 5);  // Only clamp at bottom of window
            
            cairo_move_to(cr, label_x, label_y);
            cairo_show_text(cr, freq_label);
        }
    }
    
    if (local_data) {
        g_print("Freeing local buffer %p\n", (void*)local_data);
        g_free(local_data);
    }

    scope->drawing_in_progress = FALSE;  // Add this line before return
    
    return TRUE;
}
struct ScopeWindow* scope_window_create(GtkWidget *parent, struct ParameterStore *params) {
    g_print("Creating scope window\n");
    
    struct ScopeWindow *scope = g_new0(struct ScopeWindow, 1);
    scope->params = params;
    
    // Initialize data buffer
    scope->data_size = SCOPE_BUFFER_SIZE;
    scope->waveform_data = g_malloc(sizeof(float) * scope->data_size * 2);
    scope->write_pos = 0;
    
    // Initialize display parameters
    scope->time_scale = 1.0f;
    scope->volt_scale = 1.0f;
    scope->trigger_level = 0.0f;
    scope->auto_trigger = TRUE;
    scope->window_width = 1200;
    scope->window_height = 800;
    scope->size_changed = FALSE;
    scope->time_per_div = 1.0f;
    
    // Initialize trigger info
    scope->trigger.position = 0;
    scope->trigger.value = 0.0f;
    scope->trigger.valid = FALSE;
    scope->drawing_in_progress = FALSE;  

    
    // Initialize FFT analyzer
    scope->fft = fft_analyzer_create();
    if (!scope->fft) {
        g_print("Failed to create FFT analyzer\n");
        g_free(scope->waveform_data);
        g_free(scope);
        return NULL;
    }
    
    scope->show_fft = TRUE;
    scope->fft_data = g_malloc(sizeof(float) * (FFT_SIZE/2 + 1));
    if (!scope->fft_data) {
        g_print("Failed to allocate FFT display buffer\n");
        fft_analyzer_destroy(scope->fft);
        g_free(scope->waveform_data);
        g_free(scope);
        return NULL;
    }
    
    memset(scope->fft_data, 0, sizeof(float) * (FFT_SIZE/2 + 1));
    
    g_mutex_init(&scope->data_mutex);
    g_mutex_init(&scope->update_mutex);
    
    // Create drawing area
    scope->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(scope->drawing_area, 
                             scope->window_width, 
                             scope->window_height);
    g_object_set_data(G_OBJECT(scope->drawing_area), "scope", scope);
    gtk_widget_set_hexpand(scope->drawing_area, TRUE);
    gtk_widget_set_vexpand(scope->drawing_area, TRUE);
    
    // Connect signals
    g_signal_connect(scope->drawing_area, "draw", 
                    G_CALLBACK(on_draw), NULL);
    g_signal_connect(scope->drawing_area, "size-allocate", 
                    G_CALLBACK(on_size_allocate), scope);
    
    // Add to parent
    gtk_container_add(GTK_CONTAINER(parent), scope->drawing_area);
    
    return scope;
}

void scope_window_destroy(struct ScopeWindow *scope) {
    if (!scope) return;
    
    g_mutex_lock(&scope->data_mutex);
    if (scope->waveform_data) {
        g_free(scope->waveform_data);
        scope->waveform_data = NULL;
    }
    if (scope->fft_data) {
        g_free(scope->fft_data);
        scope->fft_data = NULL;
    }
    g_mutex_unlock(&scope->data_mutex);
    
    if (scope->fft) {
        fft_analyzer_destroy(scope->fft);
        scope->fft = NULL;
    }
    
    g_mutex_clear(&scope->data_mutex);
    g_mutex_clear(&scope->update_mutex);
    
    g_free(scope);
}

void scope_window_update_data(struct ScopeWindow *scope, const float *data, size_t count) {
    if (!scope || !data || count == 0) return;
    
    g_mutex_lock(&scope->data_mutex);
    size_t samples_to_copy = MIN(count, scope->data_size);
    memcpy(scope->waveform_data, data, samples_to_copy * sizeof(float) * 2);
    scope->write_pos = samples_to_copy;
    g_mutex_unlock(&scope->data_mutex);
    
    // Modify this if check
    if (scope->drawing_area && GTK_IS_WIDGET(scope->drawing_area) && !scope->drawing_in_progress) {
        gtk_widget_queue_draw(scope->drawing_area);
    }
}


void scope_window_downsample_buffer(const float *source_buffer, size_t source_samples,
                                  float *display_buffer, size_t display_width,
                                  size_t trigger_position) {
    // Safety checks
    if (!source_buffer || !display_buffer || source_samples == 0 || 
        display_width == 0 || trigger_position >= source_samples) {
        g_print("Scope: Invalid parameters for downsampling\n");
        memset(display_buffer, 0, display_width * sizeof(float));
        return;
    }

    // Calculate safe boundaries
    size_t safe_samples = MIN(source_samples, SCOPE_BUFFER_SIZE);
    
    // Calculate safe averaging window
    float samples_per_pixel = (float)safe_samples / display_width;
    
    // Clear buffer first
    memset(display_buffer, 0, display_width * sizeof(float));
    
    // Downsample with safety checks
    for (size_t x = 0; x < display_width; x++) {
        size_t start_sample = (size_t)(x * samples_per_pixel);
        if (start_sample >= safe_samples) break;
        
        size_t samples_to_average = (size_t)samples_per_pixel;
        if (start_sample + samples_to_average > safe_samples) {
            samples_to_average = safe_samples - start_sample;
        }
        
        if (samples_to_average == 0) continue;
        
        float sum = 0.0f;
        for (size_t i = 0; i < samples_to_average; i++) {
            if ((start_sample + i) * 2 + 1 < safe_samples * 2) {
                sum += source_buffer[(start_sample + i) * 2];
            }
        }
        display_buffer[x] = sum / samples_to_average;
    }
}

void scope_window_toggle_fft(struct ScopeWindow *scope, gboolean show) {
   if (!scope) return;
   scope->show_fft = show;
   if (scope->drawing_area) {
       gtk_widget_queue_draw(scope->drawing_area);
   }
}
