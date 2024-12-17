#include "audio_manager.h"
#include "common_defs.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

void circular_buffer_init(CircularBuffer *buffer, size_t size_in_frames) {
    buffer->size = size_in_frames;
    buffer->data = g_malloc(size_in_frames * 2 * sizeof(float));
    buffer->read_pos = 0;
    buffer->write_pos = 0;
    buffer->frames_stored = 0;
    buffer->last_callback_time = 0;
    buffer->callback_count = 0;
    g_mutex_init(&buffer->mutex);
    g_cond_init(&buffer->data_ready);
    memset(buffer->data, 0, size_in_frames * 2 * sizeof(float));
}


void circular_buffer_clear(CircularBuffer *buffer) {
    g_mutex_lock(&buffer->mutex);
    buffer->read_pos = 0;
    buffer->write_pos = 0;
    buffer->frames_stored = 0;
    memset(buffer->data, 0, buffer->size * 2 * sizeof(float));
    g_mutex_unlock(&buffer->mutex);
    g_print("Circular buffer cleared and zeroed\n");
}




void circular_buffer_destroy(CircularBuffer *buffer) {
    g_mutex_clear(&buffer->mutex);
    g_cond_clear(&buffer->data_ready);
    g_free(buffer->data);
}

size_t circular_buffer_write(CircularBuffer *buffer, float *data, size_t frames) {
    g_mutex_lock(&buffer->mutex);

    // Get all values under lock
    size_t frames_available = buffer->size - buffer->frames_stored;
    size_t frames_to_write = (frames <= frames_available) ? frames : frames_available;
    size_t current_write_pos = buffer->write_pos;

    if (frames_to_write > 0) {
        // Calculate in stereo samples for safer bounds checking
        size_t stereo_write_pos = current_write_pos * 2;
        size_t stereo_buffer_size = buffer->size * 2;
        size_t stereo_frames_to_write = frames_to_write * 2;

        // Check if we can write in one chunk
        if (stereo_write_pos + stereo_frames_to_write <= stereo_buffer_size) {
            memcpy(buffer->data + stereo_write_pos, data,
                   stereo_frames_to_write * sizeof(float));
        } else {
            // Split write into two chunks
            size_t first_chunk_stereo = stereo_buffer_size - stereo_write_pos;
            memcpy(buffer->data + stereo_write_pos, data,
                   first_chunk_stereo * sizeof(float));
            memcpy(buffer->data, data + first_chunk_stereo,
                   (stereo_frames_to_write - first_chunk_stereo) * sizeof(float));
        }

        buffer->write_pos = (current_write_pos + frames_to_write) % buffer->size;
        buffer->frames_stored += frames_to_write;
    }

    g_mutex_unlock(&buffer->mutex);
    return frames_to_write;
}

size_t circular_buffer_read(CircularBuffer *buffer, float *data, size_t frames) {
    g_mutex_lock(&buffer->mutex);
    
    // Get all values we need under lock
    size_t current_frames = buffer->frames_stored;
    size_t current_read_pos = buffer->read_pos;
    size_t current_write_pos = buffer->write_pos;
    
    if (current_frames < MIN_BUFFER_FILL) {
        g_print("Buffer low (%zu < %zu), outputting silence (write_pos=%zu, read_pos=%zu)\n", 
                current_frames, (size_t)MIN_BUFFER_FILL,
                current_write_pos, current_read_pos);
        g_mutex_unlock(&buffer->mutex);
        memset(data, 0, frames * 2 * sizeof(float));
        return frames;
    }
    
    size_t frames_to_read = frames;
    if (frames_to_read > current_frames) {
        frames_to_read = current_frames;
    }
    
    if (frames_to_read > 0) {
        size_t first_chunk = buffer->size - current_read_pos;
        if (frames_to_read <= first_chunk) {
            memcpy(data, buffer->data + (current_read_pos * 2),
                   frames_to_read * 2 * sizeof(float));
        } else {
            memcpy(data, buffer->data + (current_read_pos * 2),
                   first_chunk * 2 * sizeof(float));
            memcpy(data + (first_chunk * 2), buffer->data,
                   (frames_to_read - first_chunk) * 2 * sizeof(float));
        }
        
        buffer->read_pos = (current_read_pos + frames_to_read) % buffer->size;
        buffer->frames_stored -= frames_to_read;
    }
    
    if (frames_to_read < frames) {
        memset(data + (frames_to_read * 2), 0,
               (frames - frames_to_read) * 2 * sizeof(float));
    }
    
    g_mutex_unlock(&buffer->mutex);
    return frames;
}

static int pa_callback(const void *input,
                      void *output,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData) {
    (void)input;           // Unused parameters marked explicitly
    (void)timeInfo;
    (void)statusFlags;
    
    //static bool priority_set = false;
    AudioManager *manager = (AudioManager *)userData;
    float *out = (float*)output;
    
/*
    // Set thread priority once on first callback
    if (!priority_set) {
        // macOS thread priority setting
        if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0) {
            g_print("Audio callback: Set to USER_INTERACTIVE QoS\n");
            priority_set = true;
        } else {
            g_print("Audio callback: Failed to set thread QoS\n");
        }
    }
*/   
    g_mutex_lock(&manager->buffer.mutex);
    
    // Track actual callback timing
    gint64 current_time = g_get_monotonic_time();
    
    if (manager->buffer.last_callback_time != 0) {
        gdouble interval = (current_time - manager->buffer.last_callback_time) / 1000.0;
        //g_print("Audio callback interval: %.3f ms\n", interval);
    }
    manager->buffer.last_callback_time = current_time;
    manager->buffer.callback_count++;
    
    // Signal data consumers
    g_cond_signal(&manager->buffer.data_ready);
    g_mutex_unlock(&manager->buffer.mutex);
    
    // Read from circular buffer
    circular_buffer_read(&manager->buffer, out, framesPerBuffer);
    return paContinue;
}

AudioManager* audio_manager_create(void) {
   PaError err = Pa_Initialize();
   if (err != paNoError) {
       g_print("Failed to initialize PortAudio: %s\n", Pa_GetErrorText(err));
       return NULL;
   }

   AudioManager *manager = g_new0(AudioManager, 1);
   
   g_mutex_init(&manager->mutex);
   manager->sample_rate = SAMPLE_RATE;
   manager->channels = 2;
   manager->is_active = false;
   manager->stream = NULL;
   manager->data_callback = NULL;
   manager->callback_data = NULL;
   manager->selected_device = NULL;
   
   // Initialize buffer - 4 buffers worth for safety
   circular_buffer_init(&manager->buffer, AUDIO_BUFFER_SIZE * 4);
   
   manager->output_device = Pa_GetDefaultOutputDevice();
   const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(manager->output_device);
   if (!outputInfo) {
       g_free(manager);
       return NULL;
   }
   
   manager->available_devices = g_array_new(FALSE, FALSE, sizeof(AudioDeviceInfo));
   manager->devices_updated = false;

   return manager;
}

bool audio_manager_toggle_playback(AudioManager *manager, bool enable,
                                AudioDataCallback callback, void *user_data) {
   if (!manager) return false;
   
   g_mutex_lock(&manager->mutex);
   
   if (enable == manager->is_active) {
       g_mutex_unlock(&manager->mutex);
       return true;
   }
   
   if (enable) {
       g_print("Starting audio stream setup...\n");
       // Clear and reset buffer before starting
       circular_buffer_clear(&manager->buffer);
       manager->data_callback = callback;
       manager->callback_data = user_data;

       const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(manager->output_device);
       if (!outputInfo) {
           g_print("Failed to get output device info\n");
           g_mutex_unlock(&manager->mutex);
           return false;
       }

       PaError err = Pa_OpenStream(
           &manager->stream,
           NULL,
           &(PaStreamParameters){
               .device = manager->output_device,
               .channelCount = 2,
               .sampleFormat = paFloat32,
               .suggestedLatency = outputInfo->defaultLowOutputLatency,
               .hostApiSpecificStreamInfo = NULL
           },
           SAMPLE_RATE,
           AUDIO_BUFFER_SIZE,
           paNoFlag,
           pa_callback,
           manager);

       if (err != paNoError) {
           g_print("Failed to open stream: %s\n", Pa_GetErrorText(err));
           g_mutex_unlock(&manager->mutex);
           return false;
       }

       err = Pa_StartStream(manager->stream);
       if (err != paNoError) {
           g_print("Failed to start stream: %s\n", Pa_GetErrorText(err));
           Pa_CloseStream(manager->stream);
           manager->stream = NULL;
           g_mutex_unlock(&manager->mutex);
           return false;
       }
       g_print("PortAudio stream started successfully\n");

       manager->is_active = true;
   } else {
       if (manager->stream) {
           Pa_StopStream(manager->stream);
           Pa_CloseStream(manager->stream);
           manager->stream = NULL;
       }
       manager->data_callback = NULL;
       manager->callback_data = NULL;
       circular_buffer_clear(&manager->buffer);
       manager->is_active = false;
   }
   
   g_mutex_unlock(&manager->mutex);
   return true;
}


void audio_manager_destroy(AudioManager *manager) {
   if (!manager) return;
   
   g_mutex_lock(&manager->mutex);
   
   if (manager->stream) {
       Pa_StopStream(manager->stream);
       Pa_CloseStream(manager->stream);
       manager->stream = NULL;
   }

   if (manager->available_devices) {
       for (guint i = 0; i < manager->available_devices->len; i++) {
           AudioDeviceInfo *dev = &g_array_index(manager->available_devices, AudioDeviceInfo, i);
           g_free(dev->name);
           g_free(dev->description);
       }
       g_array_free(manager->available_devices, TRUE);
   }
   g_free(manager->selected_device);
   circular_buffer_destroy(&manager->buffer);
   
   g_mutex_unlock(&manager->mutex);
   g_mutex_clear(&manager->mutex);
   
   Pa_Terminate();
   
   g_free(manager);
}




bool audio_manager_toggle_capture(AudioManager *manager, bool enable) {
    (void)manager;
    (void)enable;
    return false;
}

bool audio_manager_get_cached_devices(AudioManager *manager, char ***device_names, 
                                   char ***device_descriptions, int *count) {
   if (!manager || !device_names || !device_descriptions || !count) {
       return false;
   }

   g_mutex_lock(&manager->mutex);
   
   *count = Pa_GetDeviceCount();
   if (*count < 0) {
       g_mutex_unlock(&manager->mutex);
       return false;
   }

   *device_names = g_new(char*, *count);
   *device_descriptions = g_new(char*, *count);

   for (int i = 0; i < *count; i++) {
       const PaDeviceInfo *device_info = Pa_GetDeviceInfo(i);
       if (device_info && device_info->maxOutputChannels > 0) {
           (*device_names)[i] = g_strdup_printf("%d", i);
           (*device_descriptions)[i] = g_strdup(device_info->name);
       }
   }

   g_mutex_unlock(&manager->mutex);
   return true;
}

bool audio_manager_switch_device(AudioManager *manager, const char *device_name) {
    if (!manager || !device_name) return false;

    // Quick check of active state
    g_mutex_lock(&manager->mutex);
    bool was_active = manager->is_active;
    g_mutex_unlock(&manager->mutex);

    // Stop playback if needed
    if (was_active) {
        audio_manager_toggle_playback(manager, false, NULL, NULL);
    }

    // Update device selection with quick lock
    g_mutex_lock(&manager->mutex);
    g_free(manager->selected_device);
    manager->selected_device = g_strdup(device_name);
    PaDeviceIndex new_device = atoi(device_name);
    manager->output_device = new_device;
    g_mutex_unlock(&manager->mutex);

    // Get callback info with quick lock
    g_mutex_lock(&manager->mutex);
    AudioDataCallback callback = manager->data_callback;
    void *callback_data = manager->callback_data;
    g_mutex_unlock(&manager->mutex);

    // Restart if needed
    if (was_active) {
        return audio_manager_toggle_playback(manager, true, callback, callback_data);
    }

    return true;
}

bool audio_manager_is_playback_active(AudioManager *manager) {
    if (!manager) return false;
    
    // Quick lock for status check
    g_mutex_lock(&manager->mutex);
    bool active = manager->is_active;
    g_mutex_unlock(&manager->mutex);
    
    return active;
}
