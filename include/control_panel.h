#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#include <gtk/gtk.h>
#include "parameter_store.h"

struct ControlPanel {
    struct ParameterStore *params;
    GtkWidget *container;
    GtkWidget *waveform_combo;
    
    // Existing dials
    GtkWidget *frequency_dial;
    GtkWidget *amplitude_dial;
    GtkWidget *duty_cycle_dial;
    GtkWidget *fm_freq_dial;
    GtkWidget *fm_depth_dial;
    GtkWidget *am_freq_dial;
    GtkWidget *am_depth_dial;
    GtkWidget *dcm_freq_dial;
    GtkWidget *dcm_depth_dial;
    
    // New filter dials
    GtkWidget *filter_cutoff_dial;
    GtkWidget *filter_resonance_dial;
    GtkWidget *filter_cutoff_lfo_freq_dial;
    GtkWidget *filter_cutoff_lfo_amount_dial;
    GtkWidget *filter_res_lfo_freq_dial;
    GtkWidget *filter_res_lfo_amount_dial;
};

typedef struct ControlPanel ControlPanel;

struct ControlPanel* control_panel_create(GtkWidget *parent, struct ParameterStore *params);
void control_panel_destroy(struct ControlPanel *panel);

#endif // CONTROL_PANEL_H
