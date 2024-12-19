// fft_analyzer.c:
#include "fft_analyzer.h"
#include <math.h>
#include <string.h>

static void create_hamming_window(double *window, size_t size) {
   for (size_t i = 0; i < size; i++) {
       // Hamming window formula: 0.54 - 0.46 * cos(2π * n/(N-1))
       window[i] = 0.54 - 0.46 * cos(2.0 * M_PI * i / (size - 1));
   }
}

static void create_hann_window(double *window, size_t size, double *power) {
   double sum = 0.0;
   for (size_t i = 0; i < size; i++) {
       window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (size - 1)));
       sum += window[i] * window[i];
   }
   *power = sum / size;
}

static float mag_to_db(float magnitude) {
   return 20.0f * log10f(fmaxf(magnitude, 1e-6f));
}

struct FFTAnalyzer* fft_analyzer_create(void) {
   g_print("FFT Analyzer: Starting creation\n");
   
   struct FFTAnalyzer *analyzer = g_malloc(sizeof(struct FFTAnalyzer));
   if (!analyzer) {
       g_print("ERROR: Failed to allocate FFT analyzer structure\n");
       return NULL;
   }
   
   analyzer->input = fftw_alloc_real(FFT_SIZE);
   analyzer->output = fftw_alloc_complex(FFT_SIZE/2 + 1);
   analyzer->magnitudes = g_malloc(sizeof(float) * (FFT_SIZE/2 + 1));
   analyzer->smoothed_mags = g_malloc(sizeof(float) * (FFT_SIZE/2 + 1));
   analyzer->window = fftw_alloc_real(WINDOW_SIZE);
   
   if (!analyzer->input || !analyzer->output || !analyzer->magnitudes || 
       !analyzer->smoothed_mags || !analyzer->window) {
       fft_analyzer_destroy(analyzer);
       return NULL;
   }
   
   // Initialize buffers
   memset(analyzer->magnitudes, 0, sizeof(float) * (FFT_SIZE/2 + 1));
   memset(analyzer->smoothed_mags, 0, sizeof(float) * (FFT_SIZE/2 + 1));
   
   analyzer->size = FFT_SIZE;
   
   // Create window function
   create_hann_window(analyzer->window, WINDOW_SIZE, &analyzer->window_power);
   
   // Create FFT plan
   analyzer->plan = fftw_plan_dft_r2c_1d(FFT_SIZE, analyzer->input, 
                                        analyzer->output, FFTW_MEASURE);
   if (!analyzer->plan) {
       fft_analyzer_destroy(analyzer);
       return NULL;
   }
   
   return analyzer;
}

void fft_analyzer_destroy(struct FFTAnalyzer *analyzer) {
   if (!analyzer) return;
   
   if (analyzer->plan) fftw_destroy_plan(analyzer->plan);
   if (analyzer->input) fftw_free(analyzer->input);
   if (analyzer->output) fftw_free(analyzer->output);
   if (analyzer->window) fftw_free(analyzer->window);
   if (analyzer->magnitudes) g_free(analyzer->magnitudes);
   if (analyzer->smoothed_mags) g_free(analyzer->smoothed_mags);
   
   g_free(analyzer);
}

void fft_analyzer_process(struct FFTAnalyzer *analyzer, const float *buffer, size_t buffer_size) {
   if (!analyzer || !buffer) return;
   
   // Make a local copy of the input buffer to prevent data races
   float *temp_buffer = g_malloc(buffer_size * sizeof(float) * 2);
   if (!temp_buffer) return;
   
   memcpy(temp_buffer, buffer, buffer_size * sizeof(float) * 2);
   
   // Clear input buffer
   memset(analyzer->input, 0, sizeof(double) * FFT_SIZE);
   
   // Calculate how many samples we can safely process
   size_t samples_to_process = MIN(buffer_size, FFT_SIZE);
   
   // Copy and window the input data
   for (size_t i = 0; i < samples_to_process; i++) {
       double sample = (double)temp_buffer[i * 2];  // Use left channel
       analyzer->input[i] = sample * analyzer->window[i];
   }
   
   g_free(temp_buffer);
   
   // Perform FFT
   fftw_execute(analyzer->plan);
   
   // Process FFT output with proper scaling
   const double fft_scale = 1.0 / (FFT_SIZE * sqrt(analyzer->window_power));
   const float smoothing = 0.7f;
   
   for (size_t i = 0; i < FFT_SIZE/2 + 1; i++) {
       double real = analyzer->output[i][0] * fft_scale;
       double imag = analyzer->output[i][1] * fft_scale;
       double magnitude = sqrt(real * real + imag * imag);
       
       // Convert to dB with improved range
       float db = mag_to_db(magnitude);
       db = fmaxf(db, MIN_DB);
       db = fminf(db, MAX_DB);
       
       // Normalize to 0-1 range for display
       float normalized = (db - MIN_DB) / (MAX_DB - MIN_DB);
       
       // Apply temporal smoothing
       analyzer->smoothed_mags[i] = analyzer->smoothed_mags[i] * smoothing + 
                                  normalized * (1.0f - smoothing);
       
       analyzer->magnitudes[i] = analyzer->smoothed_mags[i];
   }
}

size_t fft_analyzer_freq_to_bin(struct FFTAnalyzer *analyzer, float freq, float sample_rate) {
   return (size_t)(freq * FFT_SIZE / sample_rate);
}

float fft_analyzer_bin_to_freq(struct FFTAnalyzer *analyzer, size_t bin, float sample_rate) {
   return (float)bin * sample_rate / FFT_SIZE;
}
