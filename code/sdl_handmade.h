#if !defined(SDL_HANDMADE_H)

struct sdl_offscreen_buffer {
    // pixels are always 32-bits wide, Memory Order BB GG RR xx
    SDL_Texture* Texture;
    void* Memory;
    int Width;
    int Height;
    int Pitch;
};

struct sdl_window_dimension {
    int Width;
    int Height;
};

struct sdl_audio_ring_buffer {
    int Size;
    int WriteCursor;
    int PlayCursor;
    void* Data;
};

struct sdl_sound_output {
    int SamplesPerSecond;
    uint32 RunningSampleIndex;
    int BytesPerSample;
    int SecondaryBufferSize;
    real32 tSine;
    int LatencySampleCount;
};


#define SDL_HANDMADE_H
#endif