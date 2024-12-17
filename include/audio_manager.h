#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <gtk/gtk.h>
#include <portaudio.h>
#include <stdbool.h>
#include "common_defs.h"  // For AUDIO_BUFFER_SIZE and SAMPLE_RATE

// Buffer management constants
#define CIRCULAR_BUFFER_MS 100
#define CIRCULAR_BUFFER_FRAMES ((SAMPLE_RATE * CIRCULAR_BUFFER_MS) / 1000)
#define BUFFER_LOW_WATERMARK ((size_t)(CIRCULAR_BUFFER_FRAMES / 4))
#define BUFFER_HIGH_WATERMARK ((size_t)(CIRCULAR_BUFFER_FRAMES * 3 / 4))
#define TARGET_WRITE_INTERVAL_MS 4
#define MIN_BUFFER_FILL ((size_t)(AUDIO_BUFFER_SIZE))
#define BUFFER_DURATION_MS ((AUDIO_BUFFER_SIZE * 1000.0) / SAMPLE_RATE)
//#define MIN_BUFFER_FILL ((size_t)(AUDIO_BUFFER_SIZE * 2))  // Double the minimum requirement


// Forward declarations
struct WaveformGenerator;

typedef size_t (*AudioDataCallback)(float *buffer, size_t frames, void *user_data);

typedef struct {
    char *name;
    char *description;
    int sample_rate;
    int channels;
} AudioDeviceInfo;

typedef struct {
    float *data;
    size_t size;
    size_t read_pos;
    size_t write_pos;
    size_t frames_stored;
    GMutex mutex;
    GCond data_ready;
    gint64 last_callback_time;
    guint callback_count;
} CircularBuffer;

struct AudioManager {
    PaStream *stream;
    bool is_active;
    AudioDataCallback data_callback;
    void *callback_data;
    GMutex mutex;
    int sample_rate;
    int channels;
    char *selected_device;
    PaDeviceIndex output_device;
    CircularBuffer buffer;
    GArray *available_devices;
    bool devices_updated;
};

typedef struct AudioManager AudioManager;

// Function declarations with proper struct tags
struct AudioManager* audio_manager_create(void);
void audio_manager_destroy(struct AudioManager *manager);
bool audio_manager_toggle_playback(struct AudioManager *manager, bool enable,
                                 AudioDataCallback callback, void *user_data);
bool audio_manager_toggle_capture(struct AudioManager *manager, bool enable);
bool audio_manager_get_cached_devices(struct AudioManager *manager, char ***device_names,
                                    char ***device_descriptions, int *count);
bool audio_manager_switch_device(struct AudioManager *manager, const char *device_name);
bool audio_manager_is_playback_active(struct AudioManager *manager);

// Circular buffer functions
void circular_buffer_init(CircularBuffer *buffer, size_t size_in_frames);
void circular_buffer_destroy(CircularBuffer *buffer);
void circular_buffer_clear(CircularBuffer *buffer);
size_t circular_buffer_write(CircularBuffer *buffer, float *data, size_t frames);
size_t circular_buffer_read(CircularBuffer *buffer, float *data, size_t frames);

#endif // AUDIO_MANAGER_H
