/*
 * main.c - The Complete Audio Lab (Final Version)
 *
 * This application combines a spectrum analyzer and a tone generator into a
 * single, feature-rich tool.
 *
 * Final Features:
 * - Added a classic "Peak Hold" display, drawn as a red line.
 * - Press 'R' to reset the peak hold lines.
 * - Corrected all compiler warnings.
 */

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// --- Constants & Enums ---
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 600
#define SAMPLE_RATE 44100
#define REC_BUFFER_SIZE 4096
#define PLAY_BUFFER_SIZE 2048

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum {
    WAVE_SINE, WAVE_SQUARE, WAVE_SAWTOOTH, WAVE_TRIANGLE
} WaveformType;

// --- Type Definitions ---
typedef struct { double real, imag; } Complex;

typedef struct {
    double x_pos;
    double db;
    double frequency;
} PeakMarker;

typedef struct {
    int is_on;
    int is_paused;
    WaveformType wave_type;
    double sweep_time;
    int sweep_up;
    double phase;
    double current_freq;
} ToneGenerator;


// --- Global App State ---
struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font_large;
    TTF_Font* font_medium;
    TTF_Font* font_small;
    int is_running;
    int is_paused;
    int trigger_lock_on;
    int auto_timebase_on;
    SDL_AudioDeviceID rec_device;
    SDL_AudioDeviceID play_device;
    Sint16 rec_buffer[REC_BUFFER_SIZE];
    double peak_hold_magnitudes[REC_BUFFER_SIZE / 2];
    PeakMarker peak_marker;
    double squelch_threshold;
    double visual_gain;
    double scope_gain;
    int scope_display_samples;
    ToneGenerator generator;
    SDL_Rect generator_button_rect;
    SDL_Rect scope_panel_rect;
    SDL_Rect spectrum_panel_rect;
    SDL_Rect controls_panel_rect;
} AppState = {
    .is_running = 1,
    .is_paused = 0,
    .trigger_lock_on = 1,
    .auto_timebase_on = 1,
    .squelch_threshold = 500.0,
    .visual_gain = 1.0,
    .scope_gain = 1.0,
    .scope_display_samples = 2048,
    .generator = { .is_on = 0, .is_paused = 0, .wave_type = WAVE_SINE, .sweep_time = 0.0, .sweep_up = 1, .phase = 0.0, .current_freq = 20.0 },
    .generator_button_rect = { SCREEN_WIDTH - 160, SCREEN_HEIGHT - 60, 150, 50 },
    .scope_panel_rect = {10, 10, 1004, 285},
    .spectrum_panel_rect = {10, 305, 1004, 285},
    .controls_panel_rect = {10, 305, 300, 285}
};

// --- Forward Declarations ---
void recording_callback(void* userdata, Uint8* stream, int len);
void playback_callback(void* userdata, Uint8* stream, int len);
void fft(Complex* x, int n);
void draw_text(const char* text, TTF_Font* font, int x, int y, SDL_Color color, int align_right);
void freq_to_note(double frequency, char* note_buffer, size_t buffer_size);
void draw_panel(const char* title, SDL_Rect rect);
void draw_scope_graticule();
void draw_spectrum_graticule();
void reset_peak_hold();


// --- Main Function ---
int main(int argc, char* argv[]) {
    // --- Initialization ---
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    AppState.window = SDL_CreateWindow("Audio Lab Professional", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    AppState.renderer = SDL_CreateRenderer(AppState.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    AppState.font_large = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 24);
    AppState.font_medium = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 16);
    AppState.font_small = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12);
    if (!AppState.font_large || !AppState.font_medium || !AppState.font_small) return 1;

    reset_peak_hold();

    // --- Audio Device Setup ---
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = REC_BUFFER_SIZE; want.callback = recording_callback;
    AppState.rec_device = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0);
    if (AppState.rec_device > 0) SDL_PauseAudioDevice(AppState.rec_device, 0);

    SDL_zero(want);
    want.freq = SAMPLE_RATE; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = PLAY_BUFFER_SIZE; want.callback = playback_callback;
    AppState.play_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (AppState.play_device > 0) SDL_PauseAudioDevice(AppState.play_device, 1);

    int trigger_offset = 0;

    // --- Main Loop ---
    while (AppState.is_running) {
        // --- Event Handling ---
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) AppState.is_running = 0;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_p: AppState.is_paused = !AppState.is_paused; break;
                    case SDLK_SPACE: AppState.generator.is_paused = !AppState.generator.is_paused; break;
                    case SDLK_r: reset_peak_hold(); break;
                    case SDLK_t: AppState.trigger_lock_on = !AppState.trigger_lock_on; break;
                    case SDLK_a: AppState.auto_timebase_on = !AppState.auto_timebase_on; break;
                    case SDLK_w: AppState.scope_gain += 0.2; break;
                    case SDLK_s: AppState.scope_gain -= 0.2; if (AppState.scope_gain < 0.1) AppState.scope_gain = 0.1; break;
                    case SDLK_UP: AppState.squelch_threshold += 50; break;
                    case SDLK_DOWN: AppState.squelch_threshold -= 50; if (AppState.squelch_threshold < 0) AppState.squelch_threshold = 0; break;
                    case SDLK_RIGHT: AppState.visual_gain += 0.05; break;
                    case SDLK_LEFT: AppState.visual_gain -= 0.05; if (AppState.visual_gain < 0) AppState.visual_gain = 0; break;
                    case SDLK_1: AppState.generator.wave_type = WAVE_SINE; break;
                    case SDLK_2: AppState.generator.wave_type = WAVE_SQUARE; break;
                    case SDLK_3: AppState.generator.wave_type = WAVE_SAWTOOTH; break;
                    case SDLK_4: AppState.generator.wave_type = WAVE_TRIANGLE; break;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.x >= AppState.generator_button_rect.x && e.button.x <= AppState.generator_button_rect.x + AppState.generator_button_rect.w &&
                    e.button.y >= AppState.generator_button_rect.y && e.button.y <= AppState.generator_button_rect.y + AppState.generator_button_rect.h) {
                    AppState.generator.is_on = !AppState.generator.is_on;
                    if (AppState.play_device > 0) SDL_PauseAudioDevice(AppState.play_device, !AppState.generator.is_on);
                }
            }
        }

        // --- Analysis (if not globally paused) ---
        if (!AppState.is_paused) {
            Complex fft_input[REC_BUFFER_SIZE];
            double rms = 0.0;
            SDL_LockAudioDevice(AppState.rec_device);
            for(int i = 0; i < REC_BUFFER_SIZE; ++i) rms += (double)AppState.rec_buffer[i] * (double)AppState.rec_buffer[i];
            rms = sqrt(rms / REC_BUFFER_SIZE);

            if (rms > AppState.squelch_threshold) {
                if (AppState.trigger_lock_on) {
                    for (int i = 1; i < REC_BUFFER_SIZE - 1; ++i) {
                        if (AppState.rec_buffer[i-1] < 0 && AppState.rec_buffer[i] >= 0) {
                            trigger_offset = i; break;
                        }
                    }
                } else { trigger_offset = 0; }

                for(int i = 0; i < REC_BUFFER_SIZE; ++i) {
                    double hann = 0.5 * (1 - cos(2 * M_PI * i / (REC_BUFFER_SIZE - 1)));
                    fft_input[i].real = (double)AppState.rec_buffer[i] * hann;
                    fft_input[i].imag = 0.0;
                }
                fft(fft_input, REC_BUFFER_SIZE);

                double max_db = -1000.0; int peak_index = 0;
                for(int i = 1; i < REC_BUFFER_SIZE / 2; ++i) {
                    double mag = sqrt(fft_input[i].real*fft_input[i].real + fft_input[i].imag*fft_input[i].imag);
                    double db = 20 * log10(mag + 1e-9);
                    if (db > max_db) { max_db = db; peak_index = i; }
                    if (db > AppState.peak_hold_magnitudes[i]) {
                        AppState.peak_hold_magnitudes[i] = db;
                    }
                }

                double max_freq = SAMPLE_RATE / 2.0;
                double target_freq = (double)peak_index / (REC_BUFFER_SIZE / 2.0) * max_freq;
                
                if (AppState.auto_timebase_on) {
                    int target_samples = (target_freq > 0) ? (4.0 * (SAMPLE_RATE / target_freq)) : 2048;
                    if (target_samples < 100) target_samples = 100;
                    if (target_samples > REC_BUFFER_SIZE) target_samples = REC_BUFFER_SIZE;
                    AppState.scope_display_samples = (0.95 * AppState.scope_display_samples) + (0.05 * target_samples);
                } else {
                    AppState.scope_display_samples = 2048;
                }

                double min_log_freq = log10(20.0), max_log_freq = log10(max_freq);
                double log_freq_range = max_log_freq - min_log_freq;
                double log_peak_freq = log10(target_freq > 0 ? target_freq : 1.0);
                double target_x = AppState.spectrum_panel_rect.x + (((log_peak_freq - min_log_freq) / log_freq_range) * AppState.spectrum_panel_rect.w);

                AppState.peak_marker.x_pos = (0.7 * AppState.peak_marker.x_pos) + (0.3 * target_x);
                AppState.peak_marker.db = (0.7 * AppState.peak_marker.db) + (0.3 * max_db);
                AppState.peak_marker.frequency = target_freq;
            } else {
                AppState.peak_marker.db *= 0.99;
                if (AppState.peak_marker.db < 20.0) { AppState.peak_marker.db = 20.0; AppState.peak_marker.frequency = 0.0; }
            }
            
            for (int i = 0; i < REC_BUFFER_SIZE / 2; ++i) {
                AppState.peak_hold_magnitudes[i] *= 0.9995;
            }
            SDL_UnlockAudioDevice(AppState.rec_device);
        }

        // --- Drawing ---
        SDL_SetRenderDrawColor(AppState.renderer, 20, 22, 25, 255);
        SDL_RenderClear(AppState.renderer);

        draw_panel("OSCILLOSCOPE", AppState.scope_panel_rect);
        draw_panel("SPECTRUM ANALYZER", AppState.spectrum_panel_rect);
        draw_scope_graticule();
        draw_spectrum_graticule();
        
        SDL_SetRenderDrawBlendMode(AppState.renderer, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(AppState.renderer, 200, 200, 220, 150);
        for (int i = 0; i < AppState.scope_display_samples - 1; ++i) {
            int sample_idx1 = (trigger_offset + i) % REC_BUFFER_SIZE;
            int sample_idx2 = (trigger_offset + i + 1) % REC_BUFFER_SIZE;
            int x1 = AppState.scope_panel_rect.x + (float)i / AppState.scope_display_samples * AppState.scope_panel_rect.w;
            int y1 = AppState.scope_panel_rect.y + AppState.scope_panel_rect.h/2 - AppState.rec_buffer[sample_idx1] * AppState.scope_panel_rect.h/2 / 32767.0 * AppState.scope_gain;
            int x2 = AppState.scope_panel_rect.x + (float)(i + 1) / AppState.scope_display_samples * AppState.scope_panel_rect.w;
            int y2 = AppState.scope_panel_rect.y + AppState.scope_panel_rect.h/2 - AppState.rec_buffer[sample_idx2] * AppState.scope_panel_rect.h/2 / 32767.0 * AppState.scope_gain;
            SDL_RenderDrawLine(AppState.renderer, x1, y1, x2, y2);
        }
        SDL_SetRenderDrawBlendMode(AppState.renderer, SDL_BLENDMODE_NONE);

        double db_range = 110.0 - 20.0;
        
        SDL_SetRenderDrawColor(AppState.renderer, 255, 0, 80, 255);
        double max_freq = SAMPLE_RATE / 2.0;
        double min_log_freq = log10(20.0), max_log_freq = log10(max_freq);
        double log_freq_range = max_log_freq - min_log_freq;
        for (int i = 1; i < REC_BUFFER_SIZE / 2; ++i) {
            double db = AppState.peak_hold_magnitudes[i];
            if (db > 20.0) {
                double freq = (double)i / (REC_BUFFER_SIZE / 2.0) * max_freq;
                double log_f = log10(freq);
                int x = AppState.spectrum_panel_rect.x + (((log_f - min_log_freq) / log_freq_range) * AppState.spectrum_panel_rect.w);
                double mag_scaled = (db - 20.0) / db_range;
                if (mag_scaled < 0.0) { mag_scaled = 0.0; }
                if (mag_scaled > 1.0) { mag_scaled = 1.0; }
                int bar_height = (int)(mag_scaled * AppState.spectrum_panel_rect.h * AppState.visual_gain);
                SDL_RenderDrawLine(AppState.renderer, x, AppState.spectrum_panel_rect.y + AppState.spectrum_panel_rect.h - bar_height, x, AppState.spectrum_panel_rect.y + AppState.spectrum_panel_rect.h - bar_height + 1);
            }
        }

        if (AppState.peak_marker.db > 20.0) {
            double mag_scaled = (AppState.peak_marker.db - 20.0) / db_range;
            if (mag_scaled < 0.0) { mag_scaled = 0.0; }
            if (mag_scaled > 1.0) { mag_scaled = 1.0; }
            int bar_height = (int)(mag_scaled * AppState.spectrum_panel_rect.h * AppState.visual_gain);
            SDL_Rect peak_bar = {(int)AppState.peak_marker.x_pos, AppState.spectrum_panel_rect.y + AppState.spectrum_panel_rect.h - bar_height, 3, bar_height};
            SDL_SetRenderDrawBlendMode(AppState.renderer, SDL_BLENDMODE_ADD);
            SDL_SetRenderDrawColor(AppState.renderer, 100, 100, 0, 255);
            SDL_Rect glow_bar = peak_bar; glow_bar.x -= 2; glow_bar.w += 4;
            SDL_RenderFillRect(AppState.renderer, &glow_bar);
            SDL_SetRenderDrawColor(AppState.renderer, 255, 255, 150, 255);
            SDL_RenderFillRect(AppState.renderer, &peak_bar);
            SDL_SetRenderDrawBlendMode(AppState.renderer, SDL_BLENDMODE_NONE);
        }

        SDL_SetRenderDrawColor(AppState.renderer, 255, 255, 255, 100);
        SDL_RenderDrawLine(AppState.renderer, 0, 300, SCREEN_WIDTH, 300);

        char buffer[128];
        const char* wave_names[] = {"SINE", "SQUARE", "SAWTOOTH", "TRIANGLE"};
        SDL_Color text_color = {200, 200, 200, 255};
        SDL_Color value_color = {0, 255, 200, 255};
        SDL_Color peak_color = {255, 255, 0, 255};
        
        int y_pos = AppState.controls_panel_rect.y + 30;
        draw_text("Squelch:", AppState.font_medium, 30, y_pos, text_color, 0);
        snprintf(buffer, sizeof(buffer), "%.0f", AppState.squelch_threshold);
        draw_text(buffer, AppState.font_medium, 280, y_pos, value_color, 1);

        y_pos += 25;
        draw_text("Spec. Gain:", AppState.font_medium, 30, y_pos, text_color, 0);
        snprintf(buffer, sizeof(buffer), "%.2fx", AppState.visual_gain);
        draw_text(buffer, AppState.font_medium, 280, y_pos, value_color, 1);

        y_pos += 25;
        draw_text("Scope Gain (W/S):", AppState.font_medium, 30, y_pos, text_color, 0);
        snprintf(buffer, sizeof(buffer), "%.2fx", AppState.scope_gain);
        draw_text(buffer, AppState.font_medium, 280, y_pos, value_color, 1);

        y_pos += 25;
        draw_text("Trigger Lock (T):", AppState.font_medium, 30, y_pos, text_color, 0);
        SDL_Color trigger_color = AppState.trigger_lock_on ? value_color : (SDL_Color){255,100,100,255};
        draw_text(AppState.trigger_lock_on ? "ON" : "OFF", AppState.font_medium, 280, y_pos, trigger_color, 1);

        y_pos += 25;
        draw_text("Auto-Timebase (A):", AppState.font_medium, 30, y_pos, text_color, 0);
        SDL_Color timebase_color = AppState.auto_timebase_on ? value_color : (SDL_Color){255,100,100,255};
        draw_text(AppState.auto_timebase_on ? "ON" : "OFF", AppState.font_medium, 280, y_pos, timebase_color, 1);

        y_pos += 25;
        draw_text("Waveform (1-4):", AppState.font_medium, 30, y_pos, text_color, 0);
        draw_text(wave_names[AppState.generator.wave_type], AppState.font_medium, 280, y_pos, value_color, 1);

        if (AppState.generator.is_on) {
            y_pos += 25;
            const char* sweep_status = AppState.generator.is_paused ? "Paused (Space)" : "Sweeping";
            SDL_Color sweep_color = AppState.generator.is_paused ? peak_color : value_color;
            draw_text("Gen Status:", AppState.font_medium, 30, y_pos, text_color, 0);
            draw_text(sweep_status, AppState.font_medium, 280, y_pos, sweep_color, 1);
        }
        
        y_pos += 25;
        draw_text("Reset Peaks (R)", AppState.font_medium, 30, y_pos, text_color, 0);

        if (AppState.peak_marker.frequency > 0.0) {
            char note_buf[16];
            freq_to_note(AppState.peak_marker.frequency, note_buf, sizeof(note_buf));
            snprintf(buffer, sizeof(buffer), "%.1f Hz", AppState.peak_marker.frequency);
            draw_text(buffer, AppState.font_large, SCREEN_WIDTH - 20, AppState.controls_panel_rect.y + 40, peak_color, 1);
            draw_text(note_buf, AppState.font_large, SCREEN_WIDTH - 20, AppState.controls_panel_rect.y + 70, peak_color, 1);
        }

        SDL_Color btn_color = AppState.generator.is_on ? (SDL_Color){0, 180, 50, 255} : (SDL_Color){150, 0, 30, 255};
        SDL_Color btn_border_color = AppState.generator.is_on ? (SDL_Color){150, 255, 180, 255} : (SDL_Color){80, 80, 80, 255};
        SDL_SetRenderDrawColor(AppState.renderer, btn_color.r, btn_color.g, btn_color.b, 255);
        SDL_RenderFillRect(AppState.renderer, &AppState.generator_button_rect);
        SDL_SetRenderDrawColor(AppState.renderer, btn_border_color.r, btn_border_color.g, btn_border_color.b, 255);
        SDL_RenderDrawRect(AppState.renderer, &AppState.generator_button_rect);
        SDL_Color btn_text_color = {255,255,255,255};
        draw_text(AppState.generator.is_on ? "GENERATOR ON" : "GENERATOR OFF", AppState.font_medium, AppState.generator_button_rect.x + 75, AppState.generator_button_rect.y + 17, btn_text_color, 0);

        if (AppState.is_paused) {
            SDL_Color paused_color = {255,0,0,255};
            draw_text("ANALYZER PAUSED (P)", AppState.font_large, SCREEN_WIDTH / 2, 15, paused_color, 0);
        }

        SDL_RenderPresent(AppState.renderer);
    }

    // --- Cleanup ---
    TTF_CloseFont(AppState.font_large); TTF_CloseFont(AppState.font_medium); TTF_CloseFont(AppState.font_small);
    if(AppState.rec_device > 0) SDL_CloseAudioDevice(AppState.rec_device);
    if(AppState.play_device > 0) SDL_CloseAudioDevice(AppState.play_device);
    SDL_DestroyRenderer(AppState.renderer);
    SDL_DestroyWindow(AppState.window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}


// --- Function Implementations ---

void fft(Complex* x, int n) {
    if (n <= 1) return;
    Complex even[n / 2], odd[n / 2];
    for (int i = 0; i < n / 2; i++) { even[i] = x[2*i]; odd[i] = x[2*i+1]; }
    fft(even, n / 2); fft(odd, n / 2);
    for (int k = 0; k < n / 2; k++) {
        double angle = -2 * M_PI * k / n;
        Complex t = {cos(angle)*odd[k].real - sin(angle)*odd[k].imag, cos(angle)*odd[k].imag + sin(angle)*odd[k].real};
        x[k].real = even[k].real + t.real; x[k].imag = even[k].imag + t.imag;
        x[k + n/2].real = even[k].real - t.real; x[k + n/2].imag = even[k].imag - t.imag;
    }
}

void recording_callback(void* userdata, Uint8* stream, int len) {
    if (!AppState.is_paused) {
        SDL_memcpy(AppState.rec_buffer, stream, len > sizeof(AppState.rec_buffer) ? sizeof(AppState.rec_buffer) : len);
    }
}

void playback_callback(void* userdata, Uint8* stream, int len) {
    Sint16* buffer = (Sint16*)stream;
    int num_samples = len / sizeof(Sint16);
    double sweep_duration_s = 20.0, start_freq = 20.0, end_freq = 5000.0;

    for (int i = 0; i < num_samples; ++i) {
        if (!AppState.generator.is_paused) {
            double sweep_progress = AppState.generator.sweep_time / sweep_duration_s;
            AppState.generator.current_freq = AppState.generator.sweep_up ?
                start_freq + (end_freq - start_freq) * sweep_progress :
                end_freq - (end_freq - start_freq) * sweep_progress;

            AppState.generator.sweep_time += 1.0 / SAMPLE_RATE;
            if (AppState.generator.sweep_time >= sweep_duration_s) {
                AppState.generator.sweep_time = 0.0;
                AppState.generator.sweep_up = !AppState.generator.sweep_up;
            }
        }

        double sample = 0.0;
        switch (AppState.generator.wave_type) {
            case WAVE_SINE: sample = sin(AppState.generator.phase); break;
            case WAVE_SQUARE: sample = sin(AppState.generator.phase) >= 0 ? 1.0 : -1.0; break;
            case WAVE_SAWTOOTH: sample = (fmod(AppState.generator.phase, 2.0 * M_PI) / M_PI) - 1.0; break;
            case WAVE_TRIANGLE: sample = 2.0 * fabs(fmod(AppState.generator.phase / (2.0 * M_PI), 1.0) * 2.0 - 1.0) - 1.0; break;
        }
        buffer[i] = (Sint16)(12000 * sample);

        AppState.generator.phase += 2.0 * M_PI * AppState.generator.current_freq / SAMPLE_RATE;
    }
    if (AppState.generator.phase > 2.0 * M_PI) AppState.generator.phase -= 2.0 * M_PI;
}

void draw_text(const char* text, TTF_Font* font, int x, int y, SDL_Color color, int align_right) {
    SDL_Surface* surface = TTF_RenderText_Blended(font, text, color);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(AppState.renderer, surface);
    SDL_Rect rect = { x, y, surface->w, surface->h };
    if (align_right) rect.x -= surface->w;
    if (y == AppState.generator_button_rect.y + 17) {
        rect.x = AppState.generator_button_rect.x + (AppState.generator_button_rect.w - surface->w) / 2;
    }
    if (x == SCREEN_WIDTH / 2) {
        rect.x -= surface->w / 2;
    }
    SDL_RenderCopy(AppState.renderer, texture, NULL, &rect);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void freq_to_note(double frequency, char* note_buffer, size_t buffer_size) {
    if (frequency <= 0) {
        snprintf(note_buffer, buffer_size, "---");
        return;
    }
    const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int note_num = (int)round(12 * log2(frequency / 440.0) + 69);
    int octave = (note_num / 12) - 1;
    int note_index = note_num % 12;
    snprintf(note_buffer, buffer_size, "%s%d", note_names[note_index], octave);
}

void draw_panel(const char* title, SDL_Rect rect) {
    SDL_SetRenderDrawColor(AppState.renderer, 40, 42, 45, 255);
    SDL_RenderFillRect(AppState.renderer, &rect);
    SDL_SetRenderDrawColor(AppState.renderer, 60, 62, 65, 255);
    SDL_RenderDrawRect(AppState.renderer, &rect);
    SDL_Color title_color = {150, 150, 150, 255};
    draw_text(title, AppState.font_small, rect.x + 5, rect.y + 5, title_color, 0);
}

void draw_scope_graticule() {
    SDL_Rect rect = AppState.scope_panel_rect;
    SDL_SetRenderDrawColor(AppState.renderer, 30, 32, 35, 255);
    for (int i = 1; i < 8; i++) {
        int y = rect.y + i * rect.h / 8;
        SDL_RenderDrawLine(AppState.renderer, rect.x, y, rect.x + rect.w, y);
    }
    for (int i = 1; i < 16; i++) {
        int x = rect.x + i * rect.w / 16;
        SDL_RenderDrawLine(AppState.renderer, x, rect.y, x, rect.y + rect.h);
    }
}

void draw_spectrum_graticule() {
    SDL_Rect rect = AppState.spectrum_panel_rect;
    SDL_SetRenderDrawColor(AppState.renderer, 30, 32, 35, 255);
    for (int i = 1; i < 6; i++) {
        int y = rect.y + i * rect.h / 6;
        SDL_RenderDrawLine(AppState.renderer, rect.x, y, rect.x + rect.w, y);
    }
    double freqs[] = {100, 200, 500, 1000, 2000, 5000, 10000};
    double max_freq = SAMPLE_RATE / 2.0;
    double min_log_freq = log10(20.0), max_log_freq = log10(max_freq);
    double log_freq_range = max_log_freq - min_log_freq;
    for (int i = 0; i < 7; i++) {
        double log_f = log10(freqs[i]);
        int x = rect.x + (((log_f - min_log_freq) / log_freq_range) * rect.w);
        if (x > rect.x && x < rect.x + rect.w) {
            SDL_RenderDrawLine(AppState.renderer, x, rect.y, x, rect.y + rect.h);
        }
    }
}

void reset_peak_hold() {
    for (int i = 0; i < REC_BUFFER_SIZE / 2; ++i) {
        AppState.peak_hold_magnitudes[i] = -1000.0;
    }
}
