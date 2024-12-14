#include "scope_window.h"
#include <math.h>

/*
static DisplayBuffers* display_buffers_create(size_t buffer_size) {
    DisplayBuffers *buffers = g_malloc(sizeof(DisplayBuffers));
    if (!buffers) {
        g_print("Failed to allocate DisplayBuffers structure\n");
        return NULL;
    }

    // Initialize atomic variables
    atomic_init(&buffers->write_index, 0);
    atomic_init(&buffers->display_index, 1);
    atomic_init(&buffers->process_index, 2);
    atomic_init(&buffers->new_data, FALSE);
    
    buffers->buffer_size = buffer_size;

    // Allocate the three buffers
    for (int i = 0; i < 3; i++) {
        buffers->buffers[i] = g_malloc(buffer_size * sizeof(float) * 2);
        if (!buffers->buffers[i]) {
            g_print("Failed to allocate buffer %d\n", i);
            // Clean up any buffers we did allocate
            for (int j = 0; j < i; j++) {
                g_free(buffers->buffers[j]);
            }
            g_free(buffers);
            return NULL;
        }
        // Initialize buffer to silence
        memset(buffers->buffers[i], 0, buffer_size * sizeof(float) * 2);
    }

    g_print("Successfully created triple buffer system\n");
    return buffers;
}
*/

// Add cleanup function
static void display_buffers_destroy(DisplayBuffers *buffers) {
    if (!buffers) return;
    
    for (int i = 0; i < 3; i++) {
        g_free(buffers->buffers[i]);
    }
    g_free(buffers);
}

static gboolean find_trigger_point(const float *buffer, size_t buffer_size,
                                 size_t display_width, TriggerInfo *trigger) {
    if (!buffer || buffer_size < 4 || !trigger) {
        return FALSE;
    }

    // Ensure we have enough samples for pre/post trigger
    size_t safe_size = MIN(buffer_size, SCOPE_BUFFER_SIZE);
    size_t pre_trigger_samples = display_width / 3;
    size_t post_trigger_samples = display_width - pre_trigger_samples;

    // Need at least 4 samples for slope calculation
    size_t search_start = 4;  // Changed from 2 to ensure safe prev2_idx
    size_t search_end = safe_size - 2;

    if (search_end <= search_start) {
        return FALSE;
    }

    g_print("Scope: Safe trigger search between %zu and %zu (buffer size %zu)\n",
            search_start, search_end, safe_size);

    for (size_t i = search_start; i < search_end; i++) {
        // Calculate stereo indices
        size_t stereo_idx = i * 2;
        if (stereo_idx + 2 >= safe_size * 2) {  // Ensure we have room for next sample
            break;
        }

        // Get left channel samples
        float prev_sample = buffer[stereo_idx - 2];  // Safe because i >= 4
        float curr_sample = buffer[stereo_idx];

        if (prev_sample <= 0.0f && curr_sample > 0.0f) {
            // Only use trigger point if we have enough samples after it
            if (i + post_trigger_samples < safe_size) {
                trigger->position = i;
                trigger->value = curr_sample;
                trigger->valid = TRUE;
                g_print("Scope: Found trigger at %zu, value=%.3f\n",
                       i, curr_sample);
                return TRUE;
            }
        }
    }

    return FALSE;
}


static void on_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
    (void)widget;
    ScopeWindow *scope = (ScopeWindow *)data;
    
    // Only lock if size actually changed
    int current_width, current_height;
    
    g_mutex_lock(&scope->data_mutex);
    current_width = scope->window_width;
    current_height = scope->window_height;
    g_mutex_unlock(&scope->data_mutex);
    
    if (current_width != allocation->width || current_height != allocation->height) {
        g_mutex_lock(&scope->data_mutex);
        scope->window_width = allocation->width;
        scope->window_height = allocation->height;
        scope->size_changed = TRUE;
        g_mutex_unlock(&scope->data_mutex);
        
        g_print("Scope window actual resize to: %dx%d\n", 
                allocation->width, allocation->height);
    } else {
        g_print("Scope window size_allocate (no change): %dx%d\n",
                allocation->width, allocation->height);
    }
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr) {
   g_print("\nStarting scope draw...\n");
   
   ScopeWindow *scope = (ScopeWindow *)g_object_get_data(G_OBJECT(widget), "scope");
   if (!scope) {
       g_print("Error: No scope data found for widget\n");
       return FALSE;
   }
   
   g_print("Draw: Attempting to lock update mutex...\n");
   g_mutex_lock(&scope->update_mutex);
   g_print("Draw: Attempting to lock data mutex...\n");
   g_mutex_lock(&scope->data_mutex);
   g_print("Draw: Both mutexes locked, samples=%zu\n", scope->write_pos);
   
   // Get widget dimensions
   GtkAllocation allocation;
   gtk_widget_get_allocation(widget, &allocation);
   int width = allocation.width;
   int height = allocation.height;
   
   // Draw black background
   cairo_set_source_rgb(cr, 0, 0, 0);
   cairo_paint(cr);
   
   // Draw grid
   cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
   cairo_set_line_width(cr, 1.0);
   
   // Vertical divisions
   float div_width = width / 12.0f;
   for (int i = 0; i <= 12; i++) {
       double x = i * div_width;
       cairo_move_to(cr, x, 0);
       cairo_line_to(cr, x, height);
   }
   
   // Horizontal divisions
   float div_height = height / 8.0f;
   for (int i = 0; i <= 8; i++) {
       double y = i * div_height;
       cairo_move_to(cr, 0, y);
       cairo_line_to(cr, width, y);
   }
   cairo_stroke(cr);
   
   // Draw waveform if we have data
   if (scope->write_pos > 0) {
       float *display_data = g_malloc(width * sizeof(float));
       
       // Find trigger in our stable buffer
       scope->trigger.valid = FALSE;  // Force new trigger search
       find_trigger_point(scope->waveform_data, scope->write_pos, width, &scope->trigger);
       
       if (scope->trigger.valid) {
           scope_window_downsample_buffer(scope->waveform_data, scope->write_pos,
                                        display_data, width,
                                        scope->trigger.position);
           
           // Draw waveform
           cairo_set_source_rgb(cr, 0, 1, 0);
           cairo_set_line_width(cr, 2.0);
           
           float half_height = height / 2.0f;
           float scale = height / 4.0f;
           
           cairo_move_to(cr, 0, half_height - display_data[0] * scale);
           for (int x = 1; x < width; x++) {
               cairo_line_to(cr, x, half_height - display_data[x] * scale);
           }
           cairo_stroke(cr);
           
           // Draw trigger marker after waveform
           cairo_set_source_rgb(cr, 1, 0, 0);
           cairo_set_line_width(cr, 1.0);
           int trigger_x = width/3;
           cairo_move_to(cr, trigger_x, 0);
           cairo_line_to(cr, trigger_x, height);
           cairo_stroke(cr);
       }
       
       g_free(display_data);
   }
   
   g_mutex_unlock(&scope->data_mutex);
   g_mutex_unlock(&scope->update_mutex);
   g_print("Draw: Mutexes unlocked\n");
   
   return TRUE;
}


void scope_window_update_data(ScopeWindow *scope, const float *data, size_t count) {
    if (!scope || !data || count == 0) return;
    
    g_mutex_lock(&scope->data_mutex);
    
    // Just copy the most recent buffer, up to our buffer size
    size_t samples_to_copy = MIN(count, scope->data_size);
    memcpy(scope->waveform_data, data, samples_to_copy * sizeof(float) * 2);
    scope->write_pos = samples_to_copy;
    
    g_mutex_unlock(&scope->data_mutex);
    
    if (scope->drawing_area) {
        gtk_widget_queue_draw(scope->drawing_area);
    }
}

ScopeWindow* scope_window_create(GtkWidget *parent, ParameterStore *params) {
    g_print("Creating scope window\n");
    ScopeWindow *scope = g_new0(ScopeWindow, 1);
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
    
    g_mutex_init(&scope->data_mutex);
    
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


void scope_window_destroy(ScopeWindow *scope) {
    if (!scope) return;
    
    g_mutex_lock(&scope->data_mutex);
    if (scope->waveform_data) {
        g_free(scope->waveform_data);
        scope->waveform_data = NULL;
    }
    g_mutex_unlock(&scope->data_mutex);
    
    // Add triple buffer cleanup
    if (scope->triple_buffer) {
        display_buffers_destroy(scope->triple_buffer);
        scope->triple_buffer = NULL;
    }
    
    g_mutex_clear(&scope->data_mutex);
    
    g_free(scope);
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
    size_t safe_trigger = MIN(trigger_position, safe_samples - 1);
    
    g_print("Scope: Safe downsampling %zu samples to %zu pixels (trigger at %zu)\n",
            safe_samples, display_width, safe_trigger);
    
    // Pre-trigger samples
   // size_t trigger_pixel = display_width / 3;
    float samples_per_pixel = (float)safe_samples / display_width;
    
    // Clear buffer first
    memset(display_buffer, 0, display_width * sizeof(float));
    
    // Downsample with safety checks
    for (size_t x = 0; x < display_width; x++) {
        size_t start_sample = (size_t)(x * samples_per_pixel);
        if (start_sample >= safe_samples) break;
        
        // Calculate safe averaging window
        size_t samples_to_average = (size_t)samples_per_pixel;
        if (start_sample + samples_to_average > safe_samples) {
            samples_to_average = safe_samples - start_sample;
        }
        
        if (samples_to_average == 0) continue;
        
        float sum = 0.0f;
        for (size_t i = 0; i < samples_to_average; i++) {
            if ((start_sample + i) * 2 + 1 < safe_samples * 2) {  // Check stereo bounds
                sum += source_buffer[(start_sample + i) * 2];
            }
        }
        display_buffer[x] = sum / samples_to_average;
    }
}
