#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#include <gtk/gtk.h>
#include "parameter_store.h"

typedef struct {
    GtkWidget *container;
    GtkWidget *waveform_combo;
    GtkWidget *frequency_dial;
    GtkWidget *amplitude_dial;
    GtkWidget *duty_cycle_dial;
    GtkWidget *fm_freq_dial;
    GtkWidget *fm_depth_dial;
    GtkWidget *am_freq_dial;
    GtkWidget *am_depth_dial;
    GtkWidget *dcm_freq_dial;
    GtkWidget *dcm_depth_dial;
    ParameterStore *params;
} ControlPanel;


ControlPanel* control_panel_create(GtkWidget *parent, ParameterStore *params);
void control_panel_destroy(ControlPanel *panel);

#endif // CONTROL_PANEL_H
