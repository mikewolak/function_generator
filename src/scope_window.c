#include "scope_window.h"
#include <math.h>

static gboolean find_trigger_point(const float *buffer, size_t buffer_size, 
                                 size_t display_width, TriggerInfo *trigger) {
    // Need enough samples for pre and post trigger display
    size_t pre_trigger_samples = display_width / 3;  // 1/3 of display for pre-trigger
    size_t post_trigger_samples = display_width - pre_trigger_samples;
    
    // Search range starts after we have enough pre-trigger samples
    for (size_t i = pre_trigger_samples + 1; i < buffer_size - post_trigger_samples; i++) {
        float prev_sample = buffer[(i-1) * 2];  // Left channel
        float curr_sample = buffer[i * 2];
        
        // Look for rising zero crossing
        if (prev_sample <= 0.0f && curr_sample > 0.0f) {
            trigger->position = i;
            trigger->value = curr_sample;
            trigger->valid = TRUE;
            
            g_print("Found trigger: pos=%zu pre=%zu post=%zu\n", 
                    i, pre_trigger_samples, post_trigger_samples);
            return TRUE;
        }
    }
    
    trigger->valid = FALSE;
    return FALSE;
}

static void on_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
    (void)widget;  // Mark parameter as intentionally unused
    ScopeWindow *scope = (ScopeWindow *)data;
    
    g_mutex_lock(&scope->data_mutex);
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


static gboolean on_draw(GtkWidget *widget, cairo_t *cr) {
    ScopeWindow *scope = (ScopeWindow *)g_object_get_data(G_OBJECT(widget), "scope");
    
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
    
    // Draw waveform
    g_mutex_lock(&scope->data_mutex);
    
    if (scope->write_pos > 0) {
        // Find trigger point before downsampling
        find_trigger_point(scope->waveform_data, scope->write_pos, width, &scope->trigger);
        
        float *display_data = g_malloc(width * sizeof(float));
        
        if (scope->trigger.valid) {
            scope_window_downsample_buffer(scope->waveform_data, scope->write_pos,
                                         display_data, width,
                                         scope->trigger.position);
        } else {
            // If no trigger, downsample from start
            scope_window_downsample_buffer(scope->waveform_data, scope->write_pos,
                                         display_data, width, 
                                         scope->write_pos / 3);  // Default position
        }
        
        cairo_set_source_rgb(cr, 0, 1, 0);
        cairo_set_line_width(cr, 2.0);
        
        float half_height = height / 2.0f;
        float scale = height / 4.0f;
        
        cairo_move_to(cr, 0, half_height - display_data[0] * scale);
        
        for (int x = 1; x < width; x++) {
            cairo_line_to(cr, x, half_height - display_data[x] * scale);
        }
        cairo_stroke(cr);
        
        // Draw trigger indicator in red on top
        if (scope->trigger.valid) {
            g_print("Drawing trigger indicator: valid=%d, pos=%zu\n", 
                   scope->trigger.valid, scope->trigger.position);
                   
            cairo_set_source_rgb(cr, 1, 0, 0);  // Bright red
            cairo_set_line_width(cr, 1.0);      // Thinner line
            
            // Draw vertical line at trigger position
            int trigger_x = width/3;
            cairo_move_to(cr, trigger_x, 0);
            cairo_line_to(cr, trigger_x, height);
            cairo_stroke(cr);
            
            // Add a small triangle marker at the center
            int triangle_size = 10;
            cairo_move_to(cr, trigger_x - triangle_size, half_height);
            cairo_line_to(cr, trigger_x + triangle_size, half_height);
            cairo_line_to(cr, trigger_x, half_height + triangle_size);
            cairo_close_path(cr);
            cairo_fill(cr);
        } else {
            g_print("No valid trigger to draw\n");
        }
        
        g_free(display_data);
    }
    
    g_mutex_unlock(&scope->data_mutex);
    
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
    
    g_mutex_clear(&scope->data_mutex);
    
    g_free(scope);
}

void scope_window_downsample_buffer(const float *source_buffer, size_t source_samples,
                                  float *display_buffer, size_t display_width,
                                  size_t trigger_position) {
    if (!source_buffer || !display_buffer || source_samples == 0 || display_width == 0) {
        return;
    }

    // Calculate display positions
    size_t trigger_pixel = display_width / 3;  // Trigger at 1/3 of screen
    size_t pre_trigger_pixels = trigger_pixel;
    size_t post_trigger_pixels = display_width - trigger_pixel;
    
    // Calculate sample offsets based on trigger
    float pre_trigger_samples_per_pixel = (float)trigger_position / pre_trigger_pixels;
    float post_trigger_samples_per_pixel = (float)(source_samples - trigger_position) / post_trigger_pixels;
    
    // Fill pre-trigger portion
    for (size_t x = 0; x < pre_trigger_pixels; x++) {
        float sum = 0.0f;
        size_t start_sample = trigger_position - 
                            (pre_trigger_pixels - x) * pre_trigger_samples_per_pixel;
        size_t samples_to_average = pre_trigger_samples_per_pixel;
        
        if (start_sample + samples_to_average > trigger_position) {
            samples_to_average = trigger_position - start_sample;
        }
        
        for (size_t i = 0; i < samples_to_average; i++) {
            sum += source_buffer[(start_sample + i) * 2];
        }
        display_buffer[x] = samples_to_average > 0 ? sum / samples_to_average : 0;
    }
    
    // Fill post-trigger portion
    for (size_t x = trigger_pixel; x < display_width; x++) {
        float sum = 0.0f;
        size_t start_sample = trigger_position + 
                            (x - trigger_pixel) * post_trigger_samples_per_pixel;
        size_t samples_to_average = post_trigger_samples_per_pixel;
        
        if (start_sample + samples_to_average > source_samples) {
            samples_to_average = source_samples - start_sample;
        }
        
        for (size_t i = 0; i < samples_to_average; i++) {
            sum += source_buffer[(start_sample + i) * 2];
        }
        display_buffer[x] = samples_to_average > 0 ? sum / samples_to_average : 0;
    }
}
