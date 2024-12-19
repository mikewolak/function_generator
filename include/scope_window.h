// scope_window.h
#ifndef SCOPE_WINDOW_H
#define SCOPE_WINDOW_H

#include <gtk/gtk.h>
#include "parameter_store.h"
#include "fft_analyzer.h"
#include "common_defs.h"

// Keep the original struct definition
struct TriggerInfo {
    size_t position;
    float value;
    gboolean valid;
};

struct ScopeWindow {
    struct ParameterStore *params;
    size_t data_size;
    float *waveform_data;
    size_t write_pos;
    
    // Display parameters
    float time_scale;
    float volt_scale;
    float trigger_level;
    gboolean auto_trigger;
    int window_width;
    int window_height;
    gboolean size_changed;
    float time_per_div;
    
    // Trigger info
    struct TriggerInfo trigger;
    
    // Drawing area
    GtkWidget *drawing_area;
    
    // Synchronization
    GMutex data_mutex;
    GMutex update_mutex;
    
    // FFT Analysis
    struct FFTAnalyzer *fft;
    float *fft_data;
    gboolean show_fft;
    int fft_height;

    gboolean drawing_in_progress; 


};

// Function declarations
struct ScopeWindow* scope_window_create(GtkWidget *parent, struct ParameterStore *params);
void scope_window_destroy(struct ScopeWindow *scope);
void scope_window_update_data(struct ScopeWindow *scope, const float *data, size_t count);
void scope_window_downsample_buffer(const float *source_buffer, size_t source_samples,
                                  float *display_buffer, size_t display_width,
                                  size_t trigger_position);
void scope_window_toggle_fft(struct ScopeWindow *scope, gboolean show);

#endif // SCOPE_WINDOW_H
