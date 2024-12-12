// waveform_dial.h
#ifndef WAVEFORM_DIAL_H
#define WAVEFORM_DIAL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define WAVEFORM_TYPE_DIAL (waveform_dial_get_type())
#define WAVEFORM_DIAL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), WAVEFORM_TYPE_DIAL, WaveformDial))
#define WAVEFORM_IS_DIAL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), WAVEFORM_TYPE_DIAL))

typedef struct _WaveformDial WaveformDial;
typedef struct _WaveformDialClass WaveformDialClass;

typedef void (*WaveformDialCallback)(WaveformDial *dial, float value, gpointer user_data);

GType waveform_dial_get_type(void) G_GNUC_CONST;
GtkWidget *waveform_dial_new(const char *label, float min, float max, float step);
void waveform_dial_set_value(WaveformDial *dial, float value);
float waveform_dial_get_value(WaveformDial *dial);
void waveform_dial_set_callback(WaveformDial *dial, WaveformDialCallback callback, gpointer user_data);

G_END_DECLS

#endif // WAVEFORM_DIAL_H
