// fft_analyzer.h
#ifndef FFT_ANALYZER_H
#define FFT_ANALYZER_H

#include <fftw3.h>
#include <glib.h>

#define FFT_SIZE 4096
#define WINDOW_SIZE FFT_SIZE
#define MIN_DB -80.0f
#define MAX_DB 0.0f

struct FFTAnalyzer {
   fftw_plan plan;
   double *window;
   double *input;
   fftw_complex *output;
   float *magnitudes;
   float *smoothed_mags;
   size_t size;
   double window_power;
};

typedef struct FFTAnalyzer FFTAnalyzer;

// Function declarations
struct FFTAnalyzer* fft_analyzer_create(void);
void fft_analyzer_destroy(struct FFTAnalyzer *analyzer);
void fft_analyzer_process(struct FFTAnalyzer *analyzer, const float *buffer, size_t buffer_size);
size_t fft_analyzer_freq_to_bin(struct FFTAnalyzer *analyzer, float freq, float sample_rate);
float fft_analyzer_bin_to_freq(struct FFTAnalyzer *analyzer, size_t bin, float sample_rate);

#endif // FFT_ANALYZER_H
