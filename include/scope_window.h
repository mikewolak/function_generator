#ifndef SCOPE_WINDOW_H
#define SCOPE_WINDOW_H

#include <gtk/gtk.h>
#include "parameter_store.h"
#include "common_defs.h"

typedef struct {
    size_t position;      // Sample position of trigger
    float value;         // Signal value at trigger
    gboolean valid;          // Whether we have a valid trigger
} TriggerInfo;


typedef struct {
    GtkWidget *drawing_area;
    float *waveform_data;      // Circular buffer for waveform data
    size_t data_size;          // Size of waveform buffer in samples
    size_t write_pos;          // Current write position in samples
    float time_scale;          // Horizontal scale (ms/div)
    float volt_scale;          // Vertical scale (V/div)
    float trigger_level;       // Trigger level
    gboolean auto_trigger;     // Auto trigger mode
    GMutex data_mutex;         // Mutex for data access
    ParameterStore *params;    // Reference to parameter store
    int window_width;          // Current window width
    int window_height;         // Current window height
    gboolean size_changed;     // Flag for window resize
    float time_per_div;        // Time base in milliseconds per division
    TriggerInfo trigger;  // Current trigger information

} ScopeWindow;

// Function declarations
ScopeWindow* scope_window_create(GtkWidget *parent, ParameterStore *params);
void scope_window_destroy(ScopeWindow *scope);
void scope_window_update_data(ScopeWindow *scope, const float *data, size_t count);
void scope_window_set_time_scale(ScopeWindow *scope, float ms_per_div);
void scope_window_set_volt_scale(ScopeWindow *scope, float volts_per_div);
void scope_window_set_trigger(ScopeWindow *scope, float level, gboolean auto_mode);

void scope_window_downsample_buffer(const float *source_buffer, size_t source_samples,
                                  float *display_buffer, size_t display_width,
                                  size_t trigger_position);

#endif // SCOPE_WINDOW_H
