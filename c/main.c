/* **************************************************************************
 * Fractal Viewer                                                           *
 * File:   main.c                                                           *
 * Author: Shirobon                                                         *
 * Date:   2023/04/01                                                       *
 * *************************************************************************/

/* ----------------------------------------------------------------------- */
/* COMPILER SETTINGS                                                       */
/* ----------------------------------------------------------------------- */
#if !defined(__STDC_VERSION__)
    #error This compiler is not reporting a C Standard version.
#endif

#if __STDC_VERSION__ < 199901L
    #error This compiler is too old. This program was written for C99 standard.
#endif

#if defined(__STDC_NO_COMPLEX__)
    #error This program relies on C99 complex number arithmetic, which is not present.
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>
#include <time.h>
#include <SDL2/SDL.h>

#define MAX_ITERATION   (80)
#define WINDOW_W        (720)
#define WINDOW_H        (480)
#define WINDOW_ASPECT   ((double)WINDOW_W / (double)WINDOW_H)

#define index           ((y * WINDOW_W) + x)

/* ----------------------------------------------------------------------- */
/* VIDEO/SYSTEM                                                            */
/* ----------------------------------------------------------------------- */
uint32_t*     g_video_fb;
SDL_Window*   g_window;
SDL_Renderer* g_renderer;
SDL_Texture*  g_fb_texture;

static inline void video_put_pixel_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    g_video_fb[(y * WINDOW_W) + x] = ((r << 24) | (g << 16) | (b << 8) | 0xFF);
}

bool video_initialize(void)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    
    g_window = SDL_CreateWindow("Fractal Viewer",
                                SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED,
                                WINDOW_W,
                                WINDOW_H,
                                SDL_WINDOW_SHOWN);
                                
    if(g_window == NULL) return false;

    g_renderer = SDL_CreateRenderer(g_window, -1, 0);
    
    if(g_renderer == NULL) return false;
    
    g_fb_texture = SDL_CreateTexture( g_renderer, 
                                      SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      WINDOW_W,
                                      WINDOW_H);
                                      
    if(g_fb_texture == NULL) return false;
    
    return true;
}

void video_close(void)
{
    SDL_DestroyTexture(g_fb_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    SDL_Quit(); 
}

void video_request_framebuffer(void)
{
    int pitch; //unused but necessary for SDL_LockTexture();
    SDL_LockTexture(g_fb_texture, NULL, (void**)&g_video_fb, &pitch);
}

void video_commit_framebuffer(void)
{
    SDL_UnlockTexture(g_fb_texture);
    SDL_RenderCopy(g_renderer, g_fb_texture, NULL, NULL);    
    SDL_RenderPresent(g_renderer);
}

/* ----------------------------------------------------------------------- */
/* FRACTAL CALCULATION                                                     */
/* ----------------------------------------------------------------------- */
double complex* g_field;  // Field of coordinate values (c)
double complex* g_set;    // Result of iterating function f(z)
int*            g_result; // Result of iterations necessary to diverge (or not)
float           g_render_time;

bool fractal_allocate(void)
{
    g_field  = malloc(sizeof(double complex) * WINDOW_W * WINDOW_H);
    g_set    = malloc(sizeof(double complex) * WINDOW_W * WINDOW_H);
    g_result = malloc(sizeof(int)            * WINDOW_W * WINDOW_H);
    
    if(!g_field || !g_set || !g_result) { 
        free(g_field);
        free(g_set);
        free(g_result);
        return false;
    }
    return true;
}

void fractal_free(void)
{
    free(g_field);
    free(g_set);
    free(g_result);
}

   
void fractal_generate(double complex origin, double zoom)
{
    memset(g_field,  0, sizeof(double complex) * WINDOW_W * WINDOW_H);
    memset(g_set,    0, sizeof(double complex) * WINDOW_W * WINDOW_H);
    memset(g_result, 0, sizeof(int)            * WINDOW_W * WINDOW_H);
    // By default the "viewport" dimensions are normalized to 2. So for zoom=1,
    // it will span from -2 to 2 around the origin. If we want to zoom in
    // twice as much (zoom=2), it then becomes -0.5 to 0.5.
    // However, we need to take the window aspect ratio into account.
    // If 720:480 (1.5), then that factor is applied to the X direction, so
    // things aren't stretched or squashed, and are in proportion with 
    // the vertical direction.
    double left_bound   = creal(origin) - (2.0 * WINDOW_ASPECT / zoom);
    double right_bound  = creal(origin) + (2.0 * WINDOW_ASPECT / zoom);
    double top_bound    = cimag(origin) + (2.0 / zoom);
    double bottom_bound = cimag(origin) - (2.0 / zoom);
    
    // Imagine a 4 element wide grid that we want equally spaced from -1 to 1
    // [-1] [-0.33] [0.33] [1], the increment is 0.66, to achieve this, we will
    // do (distance)/(gridsize-1), e.g. (1-(-1))/(4-1) = 2/3 = 0.66, epic.
    double x_incr = (right_bound - left_bound) / (WINDOW_W-1);
    double y_incr = (top_bound - bottom_bound) / (WINDOW_H-1);

    for(int y = 0; y < WINDOW_H; y++) {
        for(int x = 0; x < WINDOW_W; x++) {
            g_field[index] = (left_bound + (x * x_incr)) + (top_bound - (y * y_incr))*I;
        }
    }

    clock_t start_time = clock();
    
    for(int iteration = 0; iteration <= MAX_ITERATION; iteration++) {
        #pragma omp parallel for
        for(int y = 0; y < WINDOW_H; y++) {
            for(int x = 0; x < WINDOW_W; x++) {
                // Calculate z_n+1 = (|Re(z_n)| + i|Im(z_n)|)^2 + c
                double complex z     = g_set[index];
                double complex c     = g_field[index];
                double         re    = fabs(creal(z));
                double         im    = fabs(cimag(z));
                
                z = (re - im*I) * (re - im*I) + c;
                //z = z * z + c;
                double magnitude = cabs(z);
                
                // If magnitude is greater than 2, guaranteed to diverge
                int factor = (!(int)(magnitude / 2.0)); // m < 2 ? 1 : 0
                // If diverge, factor is zero, so multiply with z to prevent overflow
                // Also if diverge, set c to be a value to instantly diverge (>2)
                g_set[index] = z * factor;
                g_field[index] = c + (c * (1-factor) * 2.0);
                // If not diverge, increment the amount of iterations
                g_result[index] += factor;
            }
        }
    }

    clock_t end_time = clock();        
    g_render_time = (float)(end_time - start_time) / CLOCKS_PER_SEC;
}

void fractal_display(void)
{
    video_request_framebuffer();

    uint8_t step = (255 / (MAX_ITERATION+1));
    #define col(x) (255 - x*step)

    for(int y = 0; y < WINDOW_H; y++) {
        for(int x = 0; x < WINDOW_W; x++) {
            int it = g_result[index];
            video_put_pixel_rgb(x, y, col(it), col(it), col(it));
        }
    }
    video_commit_framebuffer();
}

/* ----------------------------------------------------------------------- */
/* MAIN APPLICATION                                                        */
/* ----------------------------------------------------------------------- */
int main(int argc, char* argv[])
{
    if(video_initialize() == false) {
        printf("Error while initializing video: %s\nExiting.\n", SDL_GetError());
        return 1;
    }
    
    if(fractal_allocate() == false) {
        printf("Error while allocating memory for fractal generator.\n");
        return 1;
    }
    
    SDL_Event event;
    bool running = true;
    bool need_to_render = true;
    
    double complex current_origin = 0;
    double current_zoom = 1;
 
    while(running) {
        while(SDL_PollEvent(&event) != 0) {
            
            if(event.type == SDL_QUIT) {
                running = false;
            }
            if(event.type == SDL_MOUSEMOTION) {
                printf("x: %4d    y: %4d\n", event.motion.x, event.motion.y);
            }
            if(event.type == SDL_MOUSEWHEEL) {
                int32_t mx, my;
                SDL_GetMouseState(&mx, &my);
                
                double old_zoom = current_zoom;
                
                if(event.wheel.y > 0) {
                    current_zoom += 0.1*current_zoom;
                }
                else if(event.wheel.y < 0) {
                    current_zoom -= 0.1*current_zoom;
                    if(current_zoom < 1.0) {
                        current_zoom = 1.0;
                    }
                }
   
                double left_bound   = creal(current_origin) - (2.0 * WINDOW_ASPECT / old_zoom);
                double right_bound  = creal(current_origin) + (2.0 * WINDOW_ASPECT / old_zoom);
                double top_bound    = cimag(current_origin) + (2.0 / old_zoom);
                double bottom_bound = cimag(current_origin) - (2.0 / old_zoom);
                
                double x_incr = (right_bound - left_bound) / (WINDOW_W-1);
                double y_incr = (top_bound - bottom_bound) / (WINDOW_H-1);
                
                double zoom_change = (current_zoom - old_zoom) / old_zoom;
                double complex old_origin = current_origin;
                double complex offset = (left_bound + (mx * x_incr)) + (top_bound - (my * y_incr))*I;
                offset = (offset - old_origin) * zoom_change;
                
                current_origin = old_origin + offset;
                
                printf("%f\n", current_zoom);
                need_to_render = true;
            }
        }
        if(need_to_render) {
            fractal_generate(current_origin, current_zoom);
            fractal_display();
            need_to_render = false;
        }

    }

    fractal_free();
    video_close();
    return 0;
}
