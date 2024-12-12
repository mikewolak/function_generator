#include "waveform_dial.h"
#include <math.h>

#define DIAL_SIZE 60
#define LABEL_HEIGHT 20
#define VALUE_HEIGHT 20
#define TOTAL_HEIGHT (DIAL_SIZE + LABEL_HEIGHT + VALUE_HEIGHT)

struct _WaveformDial {
    GtkDrawingArea parent;
    char *label;
    float min_value;
    float max_value;
    float value;
    float step;
    gboolean dragging;
    gdouble last_y;
    WaveformDialCallback callback;
    gpointer callback_data;
};

struct _WaveformDialClass {
    GtkDrawingAreaClass parent_class;
};

G_DEFINE_TYPE(WaveformDial, waveform_dial, GTK_TYPE_DRAWING_AREA)

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event);
static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event);
static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event);
static gboolean on_draw(GtkWidget *widget, cairo_t *cr);

static void waveform_dial_class_init(WaveformDialClass *class) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
    widget_class->draw = on_draw;
    widget_class->button_press_event = on_button_press;
    widget_class->button_release_event = on_button_release;
    widget_class->motion_notify_event = on_motion_notify;
}

static void waveform_dial_init(WaveformDial *dial) {
    gtk_widget_add_events(GTK_WIDGET(dial),
                         GDK_BUTTON_PRESS_MASK |
                         GDK_BUTTON_RELEASE_MASK |
                         GDK_POINTER_MOTION_MASK);
    
    dial->dragging = FALSE;
    dial->callback = NULL;
    dial->callback_data = NULL;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr) {
    WaveformDial *dial = WAVEFORM_DIAL(widget);
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    int width = allocation.width;
    int height = allocation.height;
    int center_x = width / 2;
    int center_y = height / 2;
    int radius = MIN(width, height) / 2 - 5;
    
    // Draw dial background
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_arc(cr, center_x, center_y, radius, 0, 2 * M_PI);
    cairo_fill(cr);
    
    // Draw outer ring
    cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
    cairo_set_line_width(cr, 2.0);
    cairo_arc(cr, center_x, center_y, radius - 1, 0, 2 * M_PI);
    cairo_stroke(cr);
    
    // Draw position indicator
    float angle = ((dial->value - dial->min_value) / 
                  (dial->max_value - dial->min_value) * 270.0 - 225.0) * M_PI / 180.0;
    
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 3.0);
    cairo_move_to(cr, center_x, center_y);
    cairo_line_to(cr,
                  center_x + cos(angle) * (radius - 5),
                  center_y + sin(angle) * (radius - 5));
    cairo_stroke(cr);
    
    // Draw center dot
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_arc(cr, center_x, center_y, 3, 0, 2 * M_PI);
    cairo_fill(cr);
    
    return TRUE;
}


GtkWidget *waveform_dial_new(const char *label, float min, float max, float step) {
    WaveformDial *dial = g_object_new(WAVEFORM_TYPE_DIAL, NULL);
    dial->label = g_strdup(label);
    dial->min_value = min;
    dial->max_value = max;
    dial->value = min;
    dial->step = step;
    
    gtk_widget_set_size_request(GTK_WIDGET(dial), DIAL_SIZE, TOTAL_HEIGHT);
    
    return GTK_WIDGET(dial);
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event) {
    WaveformDial *dial = WAVEFORM_DIAL(widget);
    if (event->button == 1) {
        dial->dragging = TRUE;
        dial->last_y = event->y;
        return TRUE;
    }
    return FALSE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event) {
    (void)event;  // Mark parameter as intentionally unused
    WaveformDial *dial = WAVEFORM_DIAL(widget);
    dial->dragging = FALSE;
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event) {
    WaveformDial *dial = WAVEFORM_DIAL(widget);
    if (dial->dragging) {
        gdouble delta_y = dial->last_y - event->y;
        
        // Convert current value to log space
        float log_min = log10f(dial->min_value > 0 ? dial->min_value : 0.1f);
        float log_max = log10f(dial->max_value);
        float log_current = log10f(dial->value > 0 ? dial->value : 0.1f);
        
        // Calculate change in log space
        float log_range = log_max - log_min;
        float log_delta = (delta_y / 400.0) * log_range;
        
        // Convert back to linear space
        float new_value = powf(10.0f, log_current + log_delta);
        new_value = CLAMP(new_value, dial->min_value, dial->max_value);
        
        if (new_value != dial->value) {
            dial->value = new_value;
            if (dial->callback) {
                dial->callback(dial, new_value, dial->callback_data);
            }
            gtk_widget_queue_draw(widget);
        }
        
        dial->last_y = event->y;
        return TRUE;
    }
    return FALSE;
}

void waveform_dial_set_value(WaveformDial *dial, float value) {
    g_return_if_fail(WAVEFORM_IS_DIAL(dial));
    dial->value = CLAMP(value, dial->min_value, dial->max_value);
    gtk_widget_queue_draw(GTK_WIDGET(dial));
}

float waveform_dial_get_value(WaveformDial *dial) {
    g_return_val_if_fail(WAVEFORM_IS_DIAL(dial), 0.0);
    return dial->value;
}

void waveform_dial_set_callback(WaveformDial *dial, WaveformDialCallback callback, gpointer user_data) {
    g_return_if_fail(WAVEFORM_IS_DIAL(dial));
    dial->callback = callback;
    dial->callback_data = user_data;
}
