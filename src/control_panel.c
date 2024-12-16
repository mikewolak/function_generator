#include "control_panel.h"
#include "waveform_dial.h"
#include <gtk/gtk.h>

static void on_waveform_changed(GtkComboBox *widget, gpointer user_data) {
    ControlPanel *panel = (ControlPanel *)user_data;
    int active = gtk_combo_box_get_active(widget);
    parameter_store_set_waveform(panel->params, active);
    
    gtk_widget_set_sensitive(panel->duty_cycle_dial, active == WAVE_SQUARE);
}

static void on_parameter_changed(WaveformDial *dial, float value, gpointer user_data) {
    ControlPanel *panel = (ControlPanel *)user_data;
    
    if (GTK_WIDGET(dial) == panel->frequency_dial) {
        parameter_store_set_frequency(panel->params, value);
    }
    else if (GTK_WIDGET(dial) == panel->amplitude_dial) {
        parameter_store_set_amplitude(panel->params, value);
    }
    else if (GTK_WIDGET(dial) == panel->duty_cycle_dial) {
        parameter_store_set_duty_cycle(panel->params, value / 100.0f);
    }
    else if (GTK_WIDGET(dial) == panel->fm_freq_dial) {
        parameter_store_set_fm(panel->params, value,
                            waveform_dial_get_value(WAVEFORM_DIAL(panel->fm_depth_dial)));
    }
    else if (GTK_WIDGET(dial) == panel->fm_depth_dial) {
        parameter_store_set_fm(panel->params,
                            waveform_dial_get_value(WAVEFORM_DIAL(panel->fm_freq_dial)),
                            value);
    }
    else if (GTK_WIDGET(dial) == panel->am_freq_dial) {
        parameter_store_set_am(panel->params, value,
                            waveform_dial_get_value(WAVEFORM_DIAL(panel->am_depth_dial)));
    }
    else if (GTK_WIDGET(dial) == panel->am_depth_dial) {
        parameter_store_set_am(panel->params,
                            waveform_dial_get_value(WAVEFORM_DIAL(panel->am_freq_dial)),
                            value);
    }
    else if (GTK_WIDGET(dial) == panel->dcm_freq_dial) {
        parameter_store_set_dcm(panel->params, value,
                             waveform_dial_get_value(WAVEFORM_DIAL(panel->dcm_depth_dial)));
    }
    else if (GTK_WIDGET(dial) == panel->dcm_depth_dial) {
        parameter_store_set_dcm(panel->params,
                             waveform_dial_get_value(WAVEFORM_DIAL(panel->dcm_freq_dial)),
                             value);
    }
    // Filter parameter handling
    else if (GTK_WIDGET(dial) == panel->filter_cutoff_dial) {
        parameter_store_set_filter_cutoff(panel->params, value);
    }
    else if (GTK_WIDGET(dial) == panel->filter_resonance_dial) {
        parameter_store_set_filter_resonance(panel->params, value);
    }
    else if (GTK_WIDGET(dial) == panel->filter_cutoff_lfo_freq_dial) {
        parameter_store_set_filter_cutoff_lfo(panel->params, value,
                                          waveform_dial_get_value(WAVEFORM_DIAL(panel->filter_cutoff_lfo_amount_dial)));
    }
    else if (GTK_WIDGET(dial) == panel->filter_cutoff_lfo_amount_dial) {
        parameter_store_set_filter_cutoff_lfo(panel->params,
                                          waveform_dial_get_value(WAVEFORM_DIAL(panel->filter_cutoff_lfo_freq_dial)),
                                          value);
    }
    else if (GTK_WIDGET(dial) == panel->filter_res_lfo_freq_dial) {
        parameter_store_set_filter_res_lfo(panel->params, value,
                                       waveform_dial_get_value(WAVEFORM_DIAL(panel->filter_res_lfo_amount_dial)));
    }
    else if (GTK_WIDGET(dial) == panel->filter_res_lfo_amount_dial) {
        parameter_store_set_filter_res_lfo(panel->params,
                                       waveform_dial_get_value(WAVEFORM_DIAL(panel->filter_res_lfo_freq_dial)),
                                       value);
    }
    
    GtkWidget *value_label = g_object_get_data(G_OBJECT(gtk_widget_get_parent(GTK_WIDGET(dial))), 
                                              "value_label");
    if (value_label) {
        char value_str[32];
        snprintf(value_str, sizeof(value_str), "%.2f", value);
        gtk_label_set_text(GTK_LABEL(value_label), value_str);
    }
}


static GtkWidget* create_dial_with_labels(const char* label_text, float min, float max, float step) {
    GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    
    GtkWidget *dial = waveform_dial_new(label_text, min, max, step);
    gtk_widget_set_size_request(dial, 80, 80);
    gtk_box_pack_start(GTK_BOX(container), dial, FALSE, FALSE, 0);
    
    GtkWidget *label = gtk_label_new(label_text);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(container), label, FALSE, FALSE, 0);
    
    GtkWidget *value_label = gtk_label_new("0.00");
    gtk_label_set_justify(GTK_LABEL(value_label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(container), value_label, FALSE, FALSE, 0);
    
    g_object_set_data(G_OBJECT(container), "dial", dial);
    g_object_set_data(G_OBJECT(container), "value_label", value_label);
    
    return container;
}

static void initialize_dial_with_value(GtkWidget *container, GtkWidget *dial, float value, 
                                     ParameterStore *params, void (*param_setter)(ParameterStore*, float)) {
    waveform_dial_set_value(WAVEFORM_DIAL(dial), value);
    
    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%.2f", value);
    GtkWidget *value_label = g_object_get_data(G_OBJECT(container), "value_label");
    if (value_label) {
        gtk_label_set_text(GTK_LABEL(value_label), value_str);
    }
    
    if (param_setter) {
        param_setter(params, value);
    }
}

ControlPanel* control_panel_create(GtkWidget *parent, ParameterStore *params) {
    ControlPanel *panel = g_new0(ControlPanel, 1);
    panel->params = params;

    panel->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(panel->container), 10);
    gtk_container_add(GTK_CONTAINER(parent), panel->container);

    // Create waveform selector
    GtkWidget *wave_frame = gtk_frame_new("Waveform Type");
    GtkWidget *wave_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(wave_frame), wave_box);
    
    panel->waveform_combo = gtk_combo_box_text_new();
    const char *waveform_names[] = {"Sine", "Square", "Sawtooth", "Triangle", "Pink Noise", NULL};
    for (const char **name = waveform_names; *name != NULL; name++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(panel->waveform_combo), *name);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(panel->waveform_combo), 0);
    gtk_box_pack_start(GTK_BOX(wave_box), panel->waveform_combo, TRUE, TRUE, 5);
    gtk_box_pack_start(GTK_BOX(panel->container), wave_frame, FALSE, FALSE, 5);

    // Create basic parameter frame
    GtkWidget *basic_frame = gtk_frame_new("Basic Parameters");
    GtkWidget *basic_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(basic_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(basic_grid), 10);
    gtk_container_add(GTK_CONTAINER(basic_frame), basic_grid);
    
    GtkWidget *freq_container = create_dial_with_labels("Frequency (Hz)", 1.0, 20000.0, 1.0);
    GtkWidget *amp_container = create_dial_with_labels("Amplitude (V)", 0.0, 3.3, 0.1);
    GtkWidget *duty_container = create_dial_with_labels("Duty Cycle (%)", 0.0, 100.0, 1.0);
    
    panel->frequency_dial = g_object_get_data(G_OBJECT(freq_container), "dial");
    panel->amplitude_dial = g_object_get_data(G_OBJECT(amp_container), "dial");
    panel->duty_cycle_dial = g_object_get_data(G_OBJECT(duty_container), "dial");

    initialize_dial_with_value(freq_container, panel->frequency_dial, 440.0f, params, parameter_store_set_frequency);
    initialize_dial_with_value(amp_container, panel->amplitude_dial, 1.0f, params, parameter_store_set_amplitude);
    initialize_dial_with_value(duty_container, panel->duty_cycle_dial, 50.0f, params, NULL);
    parameter_store_set_duty_cycle(params, 0.5f);
    
    gtk_grid_attach(GTK_GRID(basic_grid), freq_container, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(basic_grid), amp_container, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(basic_grid), duty_container, 2, 0, 1, 1);
    
    gtk_box_pack_start(GTK_BOX(panel->container), basic_frame, FALSE, FALSE, 5);

    // Create FM frame
    GtkWidget *fm_frame = gtk_frame_new("Frequency Modulation");
    GtkWidget *fm_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(fm_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(fm_grid), 10);
    gtk_container_add(GTK_CONTAINER(fm_frame), fm_grid);
    
    GtkWidget *fm_freq_container = create_dial_with_labels("FM Freq (Hz)", 0.0, 100.0, 0.1);
    GtkWidget *fm_depth_container = create_dial_with_labels("FM Depth", 0.0, 1.0, 0.01);
    
    panel->fm_freq_dial = g_object_get_data(G_OBJECT(fm_freq_container), "dial");
    panel->fm_depth_dial = g_object_get_data(G_OBJECT(fm_depth_container), "dial");

    initialize_dial_with_value(fm_freq_container, panel->fm_freq_dial, 0.0f, params, NULL);
    initialize_dial_with_value(fm_depth_container, panel->fm_depth_dial, 0.0f, params, NULL);
    parameter_store_set_fm(params, 0.0f, 0.0f);
    
    gtk_grid_attach(GTK_GRID(fm_grid), fm_freq_container, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(fm_grid), fm_depth_container, 1, 0, 1, 1);
    
    gtk_box_pack_start(GTK_BOX(panel->container), fm_frame, FALSE, FALSE, 5);

    // Create AM frame
    GtkWidget *am_frame = gtk_frame_new("Amplitude Modulation");
    GtkWidget *am_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(am_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(am_grid), 10);
    gtk_container_add(GTK_CONTAINER(am_frame), am_grid);
    
    GtkWidget *am_freq_container = create_dial_with_labels("AM Freq (Hz)", 0.0, 100.0, 0.1);
    GtkWidget *am_depth_container = create_dial_with_labels("AM Depth", 0.0, 1.0, 0.01);
    
    panel->am_freq_dial = g_object_get_data(G_OBJECT(am_freq_container), "dial");
    panel->am_depth_dial = g_object_get_data(G_OBJECT(am_depth_container), "dial");

    initialize_dial_with_value(am_freq_container, panel->am_freq_dial, 0.0f, params, NULL);
    initialize_dial_with_value(am_depth_container, panel->am_depth_dial, 0.0f, params, NULL);
    parameter_store_set_am(params, 0.0f, 0.0f);
    
    gtk_grid_attach(GTK_GRID(am_grid), am_freq_container, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(am_grid), am_depth_container, 1, 0, 1, 1);
    
    gtk_box_pack_start(GTK_BOX(panel->container), am_frame, FALSE, FALSE, 5);

    // Create DCM frame
    GtkWidget *dcm_frame = gtk_frame_new("Duty Cycle Modulation");
    GtkWidget *dcm_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(dcm_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(dcm_grid), 10);
    gtk_container_add(GTK_CONTAINER(dcm_frame), dcm_grid);
    
    GtkWidget *dcm_freq_container = create_dial_with_labels("DCM Freq (Hz)", 0.0, 100.0, 0.1);
    GtkWidget *dcm_depth_container = create_dial_with_labels("DCM Depth", 0.0, 1.0, 0.01);
    
    panel->dcm_freq_dial = g_object_get_data(G_OBJECT(dcm_freq_container), "dial");
    panel->dcm_depth_dial = g_object_get_data(G_OBJECT(dcm_depth_container), "dial");

    initialize_dial_with_value(dcm_freq_container, panel->dcm_freq_dial, 0.0f, params, NULL);
    initialize_dial_with_value(dcm_depth_container, panel->dcm_depth_dial, 0.0f, params, NULL);
    parameter_store_set_dcm(params, 0.0f, 0.0f);
    
    gtk_grid_attach(GTK_GRID(dcm_grid), dcm_freq_container, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(dcm_grid), dcm_depth_container, 1, 0, 1, 1);
    
    gtk_box_pack_start(GTK_BOX(panel->container), dcm_frame, FALSE, FALSE, 5);


     // Create filter frame
    GtkWidget *filter_frame = gtk_frame_new("Filter");
    GtkWidget *filter_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(filter_grid), 10);  // Space between dials
    gtk_container_add(GTK_CONTAINER(filter_frame), filter_grid);
    
    // Create filter dials with better names
    GtkWidget *cutoff_container = create_dial_with_labels("Cutoff", 20.0, 20000.0, 1.0);
    GtkWidget *resonance_container = create_dial_with_labels("Resonance", 0.0, 1.0, 0.01);
    GtkWidget *cutoff_lfo_freq_container = create_dial_with_labels("LFO Freq", 0.0, 20.0, 0.1);
    GtkWidget *cutoff_lfo_amt_container = create_dial_with_labels("Mod Depth", 0.0, 1.0, 0.01);
    GtkWidget *res_lfo_freq_container = create_dial_with_labels("Res LFO", 0.0, 20.0, 0.1);
    GtkWidget *res_lfo_amt_container = create_dial_with_labels("Res Mod", 0.0, 1.0, 0.01);

    // Store dial references
    panel->filter_cutoff_dial = g_object_get_data(G_OBJECT(cutoff_container), "dial");
    panel->filter_resonance_dial = g_object_get_data(G_OBJECT(resonance_container), "dial");
    panel->filter_cutoff_lfo_freq_dial = g_object_get_data(G_OBJECT(cutoff_lfo_freq_container), "dial");
    panel->filter_cutoff_lfo_amount_dial = g_object_get_data(G_OBJECT(cutoff_lfo_amt_container), "dial");
    panel->filter_res_lfo_freq_dial = g_object_get_data(G_OBJECT(res_lfo_freq_container), "dial");
    panel->filter_res_lfo_amount_dial = g_object_get_data(G_OBJECT(res_lfo_amt_container), "dial");

    // Set initial values
    initialize_dial_with_value(cutoff_container, panel->filter_cutoff_dial, 20000.0f, params, NULL);
    initialize_dial_with_value(resonance_container, panel->filter_resonance_dial, 0.0f, params, NULL);
    initialize_dial_with_value(cutoff_lfo_freq_container, panel->filter_cutoff_lfo_freq_dial, 0.0f, params, NULL);
    initialize_dial_with_value(cutoff_lfo_amt_container, panel->filter_cutoff_lfo_amount_dial, 0.0f, params, NULL);
    initialize_dial_with_value(res_lfo_freq_container, panel->filter_res_lfo_freq_dial, 0.0f, params, NULL);
    initialize_dial_with_value(res_lfo_amt_container, panel->filter_res_lfo_amount_dial, 0.0f, params, NULL);

    // Layout all dials in a single row
    gtk_grid_attach(GTK_GRID(filter_grid), cutoff_container, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(filter_grid), resonance_container, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(filter_grid), cutoff_lfo_freq_container, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(filter_grid), cutoff_lfo_amt_container, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(filter_grid), res_lfo_freq_container, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(filter_grid), res_lfo_amt_container, 5, 0, 1, 1);

    gtk_box_pack_start(GTK_BOX(panel->container), filter_frame, FALSE, FALSE, 5);

    // Connect callbacks
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->filter_cutoff_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->filter_resonance_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->filter_cutoff_lfo_freq_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->filter_cutoff_lfo_amount_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->filter_res_lfo_freq_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->filter_res_lfo_amount_dial),
                              on_parameter_changed, panel);


    // Connect all callbacks
    g_signal_connect(panel->waveform_combo, "changed",
                    G_CALLBACK(on_waveform_changed), panel);
    
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->frequency_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->amplitude_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->duty_cycle_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->fm_freq_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->fm_depth_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->am_freq_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->am_depth_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->dcm_freq_dial),
                              on_parameter_changed, panel);
    waveform_dial_set_callback(WAVEFORM_DIAL(panel->dcm_depth_dial),
                              on_parameter_changed, panel);

    return panel;
}

void control_panel_destroy(ControlPanel *panel) {
    if (!panel) return;
    g_free(panel);
}
