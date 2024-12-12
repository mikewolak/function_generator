// main.c
#include <gtk/gtk.h>
#include "window_manager.h"
#include "control_panel.h"
#include "scope_window.h"
#include "parameter_store.h"
#include "waveform_generator.h"
#include "audio_manager.h"


int main(int argc, char *argv[]) {
    g_print("Starting application\n");
    gtk_init(&argc, &argv);
    
    g_print("Creating parameter store\n");
    ParameterStore *params = parameter_store_create();
    if (!params) {
        g_print("Failed to create parameter store\n");
        return 1;
    }
    
    g_print("Creating audio manager\n");
    AudioManager *audio = audio_manager_create();
    if (!audio) {
        g_print("Warning: Failed to create audio manager, continuing without audio\n");
    }

    // Create scope window first as generator needs it
    g_print("Creating window manager\n");
    WindowManager *window_manager = window_manager_create(audio, NULL);  // Initially pass NULL for generator
    if (!window_manager) {
        g_print("Failed to create window manager\n");
        if (audio) audio_manager_destroy(audio);
        parameter_store_destroy(params);
        return 1;
    }
    
    g_print("Creating scope window\n");
    ScopeWindow *scope = scope_window_create(window_manager->scope_container, params);
    if (!scope) {
        g_print("Failed to create scope window\n");
        window_manager_destroy(window_manager);
        if (audio) audio_manager_destroy(audio);
        parameter_store_destroy(params);
        return 1;
    }

    // Now create generator with proper scope
    g_print("Creating waveform generator\n");
    WaveformGenerator *generator = waveform_generator_create(params, scope, audio);
    if (!generator) {
        g_print("Failed to create waveform generator\n");
        scope_window_destroy(scope);
        window_manager_destroy(window_manager);
        if (audio) audio_manager_destroy(audio);
        parameter_store_destroy(params);
        return 1;
    }

    // Update window manager with generator reference
    window_manager->generator = generator;
    g_print("Creating control panel\n");
    ControlPanel *control_panel = control_panel_create(window_manager->control_container, params);
    if (!control_panel) {
        g_print("Failed to create control panel\n");
        scope_window_destroy(scope);
        window_manager_destroy(window_manager);
        waveform_generator_destroy(generator);
        if (audio) audio_manager_destroy(audio);
        parameter_store_destroy(params);
        return 1;
    }
    
    g_print("Running main window\n");
    window_manager_run(window_manager);
    
    g_print("Entering main loop\n");
    gtk_main();
    
    g_print("Cleaning up\n");
    waveform_generator_destroy(generator);
    control_panel_destroy(control_panel);
    scope_window_destroy(scope);
    window_manager_destroy(window_manager);
    if (audio) audio_manager_destroy(audio);
    parameter_store_destroy(params);
    
    return 0;
}
