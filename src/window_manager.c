#include "window_manager.h"
#include <gtk/gtk.h>


typedef struct {
    WindowManager *manager;  // Add the window manager pointer
    char *device_name;
} DeviceSwitchData;


#define DEFAULT_WINDOW_WIDTH 1200
#define DEFAULT_WINDOW_HEIGHT 800
#define DEFAULT_PANE_POSITION 800

static gboolean do_device_switch_idle(gpointer user_data) {
    DeviceSwitchData *data = (DeviceSwitchData *)user_data;
    
    if (audio_manager_switch_device(data->manager->audio_manager, data->device_name)) {
        if (data->manager->generator) {
            waveform_generator_start(data->manager->generator);
            waveform_generator_set_audio_enabled(data->manager->generator, true);
        }
    }
    
    g_free(data->device_name);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void on_device_selected(GtkMenuItem *item, gpointer user_data) {
    WindowManager *manager = (WindowManager *)user_data;
    const char *device_name = g_object_get_data(G_OBJECT(item), "device_name");
    if (device_name && manager->audio_manager) {
        // Schedule device switch in idle time
        DeviceSwitchData *data = g_new(DeviceSwitchData, 1);
        data->manager = manager;
        data->device_name = g_strdup(device_name);
            g_idle_add(do_device_switch_idle, data);
        }
    }


    static gboolean update_device_menu_idle(gpointer user_data) {
        GtkWidget *menu = g_object_get_data(G_OBJECT(user_data), "menu");
        WindowManager *manager = g_object_get_data(G_OBJECT(user_data), "manager");
        
        if (!menu || !manager) {
            g_object_unref(user_data);
            return G_SOURCE_REMOVE;
        }
        
        char **device_names, **device_descriptions;
        int count;

        
        // Clear existing menu items
        GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
        g_list_foreach(children, (GFunc)gtk_widget_destroy, NULL);
        g_list_free(children);
        
        if (audio_manager_get_cached_devices(manager->audio_manager, &device_names, 
                                           &device_descriptions, &count)) {
            for (int i = 0; i < count; i++) {
                GtkWidget *item = gtk_menu_item_new_with_label(device_descriptions[i]);
                g_object_set_data_full(G_OBJECT(item), "device_name", 
                                     g_strdup(device_names[i]), g_free);
                g_signal_connect(item, "activate", G_CALLBACK(on_device_selected), manager);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
                g_free(device_names[i]);
                g_free(device_descriptions[i]);
            }
        } else {
            GtkWidget *item = gtk_menu_item_new_with_label("No devices found");
            gtk_widget_set_sensitive(item, FALSE);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
        
        g_free(device_names);
        g_free(device_descriptions);
        gtk_widget_show_all(menu);
        
        g_object_unref(user_data);
        return G_SOURCE_REMOVE;
    }

    static void on_audio_menu_shown(GtkWidget *menu, WindowManager *manager) {
        GtkWidget *device_menu = g_object_get_data(G_OBJECT(menu), "device_menu");
        if (device_menu) {
            GtkWidget *placeholder = gtk_menu_item_new_with_label("Loading devices...");
            gtk_widget_set_sensitive(placeholder, FALSE);
            gtk_menu_shell_append(GTK_MENU_SHELL(device_menu), placeholder);
            gtk_widget_show(placeholder);
            
            GObject *data = g_object_new(G_TYPE_OBJECT, NULL);
            g_object_set_data(data, "menu", device_menu);
            g_object_set_data(data, "manager", manager);
            g_object_ref(data);
            
            g_idle_add(update_device_menu_idle, data);
        }
    }

    static void on_audio_playback_toggled(GtkCheckMenuItem *item, gpointer user_data) {
        WindowManager *manager = (WindowManager *)user_data;
        bool enable = gtk_check_menu_item_get_active(item);
        if (manager->generator) {
            waveform_generator_set_audio_enabled(manager->generator, enable);
        }
    }

    static void on_audio_capture_toggled(GtkCheckMenuItem *item, gpointer user_data) {
        WindowManager *manager = (WindowManager *)user_data;
        bool enable = gtk_check_menu_item_get_active(item);
        if (manager->audio_manager) {
            audio_manager_toggle_capture(manager->audio_manager, enable);
        }
    }

    static GtkWidget* create_menubar(WindowManager *manager) {
        GtkWidget *menubar = gtk_menu_bar_new();
        
        // Audio menu
        GtkWidget *audio_menu = gtk_menu_new();
        GtkWidget *audio_item = gtk_menu_item_new_with_label("Audio");
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(audio_item), audio_menu);
        
        // Device selection submenu
        GtkWidget *device_item = gtk_menu_item_new_with_label("Output Device");
        GtkWidget *device_menu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(device_item), device_menu);
        g_object_set_data(G_OBJECT(audio_menu), "device_menu", device_menu);
        
        // Connect signal to update device menu when shown
        g_signal_connect(audio_menu, "show", G_CALLBACK(on_audio_menu_shown), manager);
        
        gtk_menu_shell_append(GTK_MENU_SHELL(audio_menu), device_item);
        
        // Separator
        GtkWidget *separator = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(audio_menu), separator);
        
        // Audio menu items
        GtkWidget *playback_item = gtk_check_menu_item_new_with_label("Enable Playback");
        GtkWidget *capture_item = gtk_check_menu_item_new_with_label("Enable Capture");
        
        // If no audio manager, disable the menu items
        if (!manager->audio_manager) {
            gtk_widget_set_sensitive(playback_item, FALSE);
            gtk_widget_set_sensitive(capture_item, FALSE);
            gtk_widget_set_sensitive(device_item, FALSE);
        }
        
        gtk_menu_shell_append(GTK_MENU_SHELL(audio_menu), playback_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(audio_menu), capture_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar), audio_item);
        
        // Connect signals
        g_signal_connect(playback_item, "toggled",
                        G_CALLBACK(on_audio_playback_toggled), manager);
        g_signal_connect(capture_item, "toggled",
                        G_CALLBACK(on_audio_capture_toggled), manager);
        
        return menubar;
    }

    static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
        (void)widget;
        (void)event;
        (void)data;
        g_print("Window close requested\n");
        gtk_main_quit();
        return FALSE;
    }

    static void on_window_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
        (void)widget;
        WindowManager *manager = (WindowManager *)data;
        manager->window_width = allocation->width;
        manager->window_height = allocation->height;
        g_print("Window resized to: %d x %d\n", allocation->width, allocation->height);
    }

    WindowManager* window_manager_create(AudioManager *audio_manager, WaveformGenerator *generator) {
        g_print("Creating window manager\n");
        
        WindowManager *manager = g_new0(WindowManager, 1);
        
        // Store audio manager and generator
        manager->audio_manager = audio_manager;
        manager->generator = generator;
        
        // Create main window
        manager->main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(manager->main_window), "Waveform Generator");
        
        // Set window properties
        gtk_window_set_default_size(GTK_WINDOW(manager->main_window), DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
        gtk_container_set_border_width(GTK_CONTAINER(manager->main_window), 10);
        gtk_window_set_resizable(GTK_WINDOW(manager->main_window), TRUE);
        
        // Create main vertical box
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(manager->main_window), vbox);
        
        // Create and add menubar
        manager->menubar = create_menubar(manager);
        gtk_box_pack_start(GTK_BOX(vbox), manager->menubar, FALSE, FALSE, 0);
        
        // Create paned container
        manager->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_box_pack_start(GTK_BOX(vbox), manager->paned, TRUE, TRUE, 0);
        
        // Create scope container
        GtkWidget *scope_frame = gtk_frame_new("Oscilloscope");
        manager->scope_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_add(GTK_CONTAINER(scope_frame), manager->scope_container);
        gtk_widget_set_size_request(scope_frame, 800, 600);
        gtk_paned_pack1(GTK_PANED(manager->paned), scope_frame, TRUE, TRUE);
        
        // Create control container
        GtkWidget *control_frame = gtk_frame_new("Controls");
        manager->control_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_container_add(GTK_CONTAINER(control_frame), manager->control_container);
        gtk_widget_set_size_request(control_frame, 300, -1);
        gtk_paned_pack2(GTK_PANED(manager->paned), control_frame, FALSE, TRUE);
        
        // Set initial pane position
        gtk_paned_set_position(GTK_PANED(manager->paned), DEFAULT_PANE_POSITION);
        
        // Connect signals
        g_signal_connect(manager->main_window, "delete-event",
                        G_CALLBACK(on_delete_event), NULL);
        g_signal_connect(manager->main_window, "size-allocate",
                        G_CALLBACK(on_window_size_allocate), manager);
        
        // Store initial size
        manager->window_width = DEFAULT_WINDOW_WIDTH;
        manager->window_height = DEFAULT_WINDOW_HEIGHT;
        
        return manager;
    }

    void window_manager_run(WindowManager *manager) {
        g_print("Showing all windows\n");
        gtk_widget_show_all(manager->main_window);
    }

    void window_manager_destroy(WindowManager *manager) {
        g_print("Destroying window manager\n");
        if (!manager) return;
        
        // Don't destroy audio_manager here since we don't own it
        g_free(manager);
}
