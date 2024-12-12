#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <gtk/gtk.h>
#include <portaudio.h>
#include <stdbool.h>

#define BUFFER_DURATION_MS ((AUDIO_BUFFER_SIZE * 1000.0) / SAMPLE_RATE)

#define CIRCULAR_BUFFER_FRAMES ((SAMPLE_RATE * CIRCULAR_BUFFER_MS) / 1000)
#define BUFFER_LOW_WATERMARK ((size_t)(CIRCULAR_BUFFER_FRAMES / 4))
#define BUFFER_HIGH_WATERMARK ((size_t)(CIRCULAR_BUFFER_FRAMES * 3 / 4))
#define TARGET_WRITE_INTERVAL_MS 4  // Slightly faster than audio callback

#define MIN_BUFFER_FILL ((size_t)(AUDIO_BUFFER_SIZE))  // Changed from AUDIO_BUFFER_SIZE * 2






typedef size_t (*AudioDataCallback)(float *buffer, size_t frames, void *user_data);

typedef struct {
    char *name;
    char *description;
    int sample_rate;
    int channels;
} AudioDeviceInfo;

// Update CircularBuffer struct to use gint64 for timing
typedef struct {
    float *data;
    size_t size;          
    size_t read_pos;      
    size_t write_pos;     
    size_t frames_stored; 
    GMutex mutex;
    GCond data_ready;     
    gint64 last_callback_time;  // Changed to gint64 for microsecond precision
    guint callback_count;
} CircularBuffer;


typedef struct {
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
    
    // Device caching
    GArray *available_devices;
    bool devices_updated;
} AudioManager;

// Function declarations
AudioManager* audio_manager_create(void);
void audio_manager_destroy(AudioManager *manager);
bool audio_manager_toggle_playback(AudioManager *manager, bool enable, 
                                 AudioDataCallback callback, void *user_data);
bool audio_manager_toggle_capture(AudioManager *manager, bool enable);
bool audio_manager_get_cached_devices(AudioManager *manager, char ***device_names, 
                                    char ***device_descriptions, int *count);
bool audio_manager_switch_device(AudioManager *manager, const char *device_name);
bool audio_manager_is_playback_active(AudioManager *manager);

// Circular buffer functions
void circular_buffer_init(CircularBuffer *buffer, size_t size_in_frames);
void circular_buffer_destroy(CircularBuffer *buffer);
void circular_buffer_clear(CircularBuffer *buffer);
size_t circular_buffer_write(CircularBuffer *buffer, float *data, size_t frames);
size_t circular_buffer_read(CircularBuffer *buffer, float *data, size_t frames);

#endif // AUDIO_MANAGER_H
