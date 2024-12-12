#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include <gtk/gtk.h>
#include "audio_manager.h"
#include "waveform_generator.h"  // Add this include

typedef struct {
    GtkWidget *main_window;
    GtkWidget *menubar;
    GtkWidget *paned;
    GtkWidget *scope_container;
    GtkWidget *control_container;
    AudioManager *audio_manager;
    WaveformGenerator *generator;  // Now WaveformGenerator is known
    int window_width;
    int window_height;
} WindowManager;

// Update function declaration
WindowManager* window_manager_create(AudioManager *audio_manager, WaveformGenerator *generator);
void window_manager_destroy(WindowManager *manager);
void window_manager_run(WindowManager *manager);

#endif // WINDOW_MANAGER_H
