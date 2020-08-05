#include <stdint.h>

#define internal static
#define local_persist static
#define global_variable static

#define Pi32 3.14159265359f

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int32 bool32;

typedef float real32;
typedef double real64;

// TODO: implement sine ourselves
#include <math.h>

#include "handmade.h"
#include "handmade.cpp"

#include <SDL.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <x86intrin.h>

#include "sdl_handmade.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

global_variable sdl_offscreen_buffer GlobalBackbuffer;

#define MAX_CONTROLLERS 4
SDL_GameController* ControllerHandles[MAX_CONTROLLERS];
SDL_Haptic* RumbleHandles[MAX_CONTROLLERS];

sdl_audio_ring_buffer AudioRingBuffer;

internal debug_read_file_result
DEBUGPlatformReadEntireFile(char* Filename) {
    debug_read_file_result Result = {};

    int FileHandle = open(Filename, O_RDONLY);
    if (FileHandle == -1) {
        return Result;
    }

    struct stat FileStatus;
    if (fstat(FileHandle, &FileStatus) == -1) {
        close(FileHandle);
        return Result;
    }
    Result.ContentsSize = SafeTruncateUInt64(FileStatus.st_size);

    Result.Contents = mmap(0, Result.ContentsSize, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                           FileHandle, 0);

    if (Result.Contents == MAP_FAILED) {
        Result.Contents = 0;
        Result.ContentsSize = 0;
    }

    close(FileHandle);
    return (Result);
}

internal void
DEBUGPlatformFreeFileMemory(void* Memory) {
    munmap(Memory, 4096);
}

internal bool32
DEBUGPlatformWriteEntireFile(char* Filename, uint32 MemorySize, void* Memory) {
    int FileHandle = open(Filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (FileHandle == -1)
        return false;

    uint32 BytesToWrite = MemorySize;
    uint8* NextByteLocation = (uint8*) Memory;
    while (BytesToWrite) {
        ssize_t BytesWritten = write(FileHandle, NextByteLocation, BytesToWrite);
        if (BytesWritten == -1) {
            close(FileHandle);
            return false;
        }
        BytesToWrite -= BytesWritten;
        NextByteLocation += BytesWritten;
    }

    close(FileHandle);

    return true;
}

internal void
SDLAudioCallback(void* UserData, Uint8* AudioData, int Length) {
    sdl_audio_ring_buffer* RingBuffer = (sdl_audio_ring_buffer*) UserData;

    int Region1Size = Length;
    int Region2Size = 0;
    if (RingBuffer->PlayCursor + Length > RingBuffer->Size) {
        Region1Size = RingBuffer->Size - RingBuffer->PlayCursor;
        Region2Size = Length - Region1Size;
    }
    memcpy(AudioData, (uint8*) (RingBuffer->Data) + RingBuffer->PlayCursor, Region1Size);
    memcpy(&AudioData[Region1Size], RingBuffer->Data, Region2Size);
    RingBuffer->PlayCursor = (RingBuffer->PlayCursor + Length) % RingBuffer->Size;
    RingBuffer->WriteCursor = (RingBuffer->PlayCursor + Length) % RingBuffer->Size;
}

internal void
SDLInitAudio(int32 SamplesPerSecond, int32 BufferSize) {
    SDL_AudioSpec AudioSettings = { 0 };

    AudioSettings.freq = SamplesPerSecond;
    AudioSettings.format = AUDIO_S16LSB;
    AudioSettings.channels = 2;
    AudioSettings.samples = 512;
    AudioSettings.callback = &SDLAudioCallback;
    AudioSettings.userdata = &AudioRingBuffer;

    AudioRingBuffer.Size = BufferSize;
    AudioRingBuffer.Data = calloc(BufferSize, 1);
    AudioRingBuffer.PlayCursor = AudioRingBuffer.WriteCursor = 0;

    SDL_OpenAudio(&AudioSettings, 0);

    printf("Initialized an Audio device at frequency %d Hz, %d Channels, buffer size %d\n",
           AudioSettings.freq, AudioSettings.channels, AudioSettings.size);

    if (AudioSettings.format != AUDIO_S16LSB) {
        printf("Oops! We didn't get AUDIO_S16LSB as our sample format!\n");
        SDL_CloseAudio();
    }
}

sdl_window_dimension
SDLGetWindowDimension(SDL_Window* Window) {
    sdl_window_dimension Result;

    SDL_GetWindowSize(Window, &Result.Width, &Result.Height);

    return (Result);
}

internal int
SDLGetWindowRefreshRate(SDL_Window* Window) {
    SDL_DisplayMode Mode;
    int DisplayIndex = SDL_GetWindowDisplayIndex(Window);
    // if we can't find the refresh rate, we'll return this:
    int DefaultRefreshRate = 60;
    if (SDL_GetDesktopDisplayMode(DisplayIndex, &Mode) != 0) {
        return DefaultRefreshRate;
    }
    if (Mode.refresh_rate == 0) {
        return DefaultRefreshRate;
    }
    return Mode.refresh_rate;
}

internal real32
SDLGetSecondsElapsed(uint64 OldCounter, uint64 CurrentCounter) {
    return ((real32) (CurrentCounter - OldCounter) / (real32) (SDL_GetPerformanceFrequency()));
}

internal void
SDLResizeTexture(sdl_offscreen_buffer* Buffer, SDL_Renderer* Renderer, int Width, int Height) {
    int BytesPerPixel = 4;
    if (Buffer->Memory) {
        munmap(Buffer->Memory, Buffer->Width * Buffer->Height * BytesPerPixel);
    }
    if (Buffer->Texture) {
        SDL_DestroyTexture(Buffer->Texture);
    }
    Buffer->Texture = SDL_CreateTexture(Renderer,
                                        SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        Width,
                                        Height);
    Buffer->Width = Width;
    Buffer->Height = Height;
    Buffer->Pitch = Width * BytesPerPixel;
    Buffer->BytesPerPixel = BytesPerPixel;
    Buffer->Memory = mmap(0,
                          Width * Height * BytesPerPixel,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1,
                          0);
}

internal void
SDLUpdateWindow(SDL_Window* Window, SDL_Renderer* Renderer, sdl_offscreen_buffer* Buffer) {
    SDL_UpdateTexture(Buffer->Texture,
                      0,
                      Buffer->Memory,
                      Buffer->Pitch);

    SDL_RenderCopy(Renderer,
                   Buffer->Texture,
                   0,
                   0);

    SDL_RenderPresent(Renderer);
}

internal void
SDLProcessKeyPress(game_button_state* NewState, bool32 IsDown) {
    Assert(NewState->EndedDown != IsDown);
    NewState->EndedDown = IsDown;
    ++NewState->HalfTransitionCount;
}

bool HandleEvent(SDL_Event* Event, game_controller_input* NewKeyboardController) {
    bool ShouldQuit = false;

    switch (Event->type) {
        case SDL_QUIT: {
            printf("SDL_QUIT\n");
            ShouldQuit = true;
        }
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            SDL_Keycode KeyCode = Event->key.keysym.sym;
            bool IsDown = (Event->key.state == SDL_PRESSED);
            bool WasDown = false;
            if (Event->key.state == SDL_RELEASED) {
                WasDown = true;
            } else if (Event->key.repeat != 0) {
                WasDown = true;
            }

            if (Event->key.repeat == 0) {
                if (KeyCode == SDLK_w) {
                    SDLProcessKeyPress(&NewKeyboardController->MoveUp, IsDown);
                } else if (KeyCode == SDLK_a) {
                    SDLProcessKeyPress(&NewKeyboardController->MoveLeft, IsDown);
                } else if (KeyCode == SDLK_s) {
                    SDLProcessKeyPress(&NewKeyboardController->MoveDown, IsDown);
                } else if (KeyCode == SDLK_d) {
                    SDLProcessKeyPress(&NewKeyboardController->MoveRight, IsDown);
                } else if (KeyCode == SDLK_q) {
                    SDLProcessKeyPress(&NewKeyboardController->LeftShoulder, IsDown);
                } else if (KeyCode == SDLK_e) {
                    SDLProcessKeyPress(&NewKeyboardController->RightShoulder, IsDown);
                } else if (KeyCode == SDLK_UP) {
                    SDLProcessKeyPress(&NewKeyboardController->ActionUp, IsDown);
                } else if (KeyCode == SDLK_LEFT) {
                    SDLProcessKeyPress(&NewKeyboardController->ActionLeft, IsDown);
                } else if (KeyCode == SDLK_DOWN) {
                    SDLProcessKeyPress(&NewKeyboardController->ActionDown, IsDown);
                } else if (KeyCode == SDLK_RIGHT) {
                    SDLProcessKeyPress(&NewKeyboardController->ActionRight, IsDown);
                } else if (KeyCode == SDLK_ESCAPE) {
                    printf("ESCAPE: ");
                    if (IsDown) {
                        printf("IsDown");
                    }
                    if (WasDown) {
                        printf("WasDown");
                    }
                    printf("\n");
                } else if (KeyCode == SDLK_SPACE) {
                }
            }
        }
            break;

        case SDL_WINDOWEVENT: {
            switch (Event->window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED: {
                    SDL_Window* Window = SDL_GetWindowFromID(Event->window.windowID);
                    SDL_Renderer* Renderer = SDL_GetRenderer(Window);
                    printf("SDL_WINDOWEVENT_SIZE_CHANGED (%d, %d)\n", Event->window.data1, Event->window.data2);
                }
                    break;

                case SDL_WINDOWEVENT_FOCUS_GAINED: {
                    printf("SDL_WINDOWEVENT_FOCUS_GAINED\n");
                }
                    break;

                case SDL_WINDOWEVENT_EXPOSED: {
                    SDL_Window* Window = SDL_GetWindowFromID(Event->window.windowID);
                    SDL_Renderer* Renderer = SDL_GetRenderer(Window);
                    SDLUpdateWindow(Window, Renderer, &GlobalBackbuffer);
                }
                    break;
            }
        }
            break;
    }

    return (ShouldQuit);
}

internal void
SDLFillSoundBuffer(sdl_sound_output* SoundOutput, int ByteToLock, int BytesToWrite,
                   game_sound_output_buffer* SoundBuffer) {
    int16* Samples = SoundBuffer->Samples;
    void* Region1 = (uint8*) AudioRingBuffer.Data + ByteToLock;
    int Region1Size = BytesToWrite;
    if (Region1Size + ByteToLock > SoundOutput->SecondaryBufferSize) {
        Region1Size = SoundOutput->SecondaryBufferSize - ByteToLock;
    }
    void* Region2 = AudioRingBuffer.Data;
    int Region2Size = BytesToWrite - Region1Size;
    int Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
    int16* SampleOut = (int16*) Region1;
    for (int SampleIndex = 0; SampleIndex < Region1SampleCount; ++SampleIndex) {
        *SampleOut++ = *Samples++;
        *SampleOut++ = *Samples++;
        ++SoundOutput->RunningSampleIndex;
    }

    int Region2SampleCount = Region2Size / SoundOutput->BytesPerSample;
    SampleOut = (int16*) Region2;
    for (int SampleIndex = 0; SampleIndex < Region2SampleCount; ++SampleIndex) {
        *SampleOut++ = *Samples++;
        *SampleOut++ = *Samples++;
        ++SoundOutput->RunningSampleIndex;
    }
}

internal void
SDLOpenGameControllers() {
    int MaxJoysticks = SDL_NumJoysticks();
    int ControllerIndex = 0;
    for (int JoystickIndex = 0; JoystickIndex < MaxJoysticks; ++JoystickIndex) {
        if (!SDL_IsGameController(JoystickIndex)) {
            continue;
        }
        if (ControllerIndex >= MAX_CONTROLLERS) {
            break;
        }

        ControllerHandles[ControllerIndex] = SDL_GameControllerOpen(JoystickIndex);
        SDL_Joystick* JoystickHandle = SDL_GameControllerGetJoystick(ControllerHandles[ControllerIndex]);
        RumbleHandles[ControllerIndex] = SDL_HapticOpenFromJoystick(JoystickHandle);
        if (SDL_HapticRumbleInit(RumbleHandles[ControllerIndex]) != 0) {
            SDL_HapticClose(RumbleHandles[ControllerIndex]);
            RumbleHandles[ControllerIndex] = 0;
        }

        ControllerIndex++;
    }
}

internal void
SDLProcessGameControllerButton(game_button_state* OldState,
                               game_button_state* NewState,
                               bool Value) {
    NewState->EndedDown = Value;
    NewState->HalfTransitionCount += ((NewState->EndedDown == OldState->EndedDown) ? 0 : 1);
}

internal real32
SDLProcessGameControllerAxisValue(int16 Value, int16 DeadZoneThreshold) {
    real32 Result = 0;

    if (Value < -DeadZoneThreshold) {
        Result = (real32) ((Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold));
    } else if (Value > DeadZoneThreshold) {
        Result = (real32) ((Value - DeadZoneThreshold) / (32767.0f - DeadZoneThreshold));
    }

    return (Result);
}

internal void
SDLCloseGameControllers() {
    for (int ControllerIndex = 0; ControllerIndex < MAX_CONTROLLERS; ++ControllerIndex) {
        if (ControllerHandles[ControllerIndex]) {
            if (RumbleHandles[ControllerIndex]) {
                SDL_HapticClose(RumbleHandles[ControllerIndex]);
            }
            SDL_GameControllerClose(ControllerHandles[ControllerIndex]);
        }
    }
}

internal void
SDLDebugDrawVertical(sdl_offscreen_buffer *GlobalBackBuffer,
                     int X, int Top, int Bottom, uint32 Color)
{
    uint8 *Pixel = ((uint8 *)GlobalBackBuffer->Memory +
            X*GlobalBackBuffer->BytesPerPixel +
            Top*GlobalBackBuffer->Pitch);
    for(int Y = Top; Y < Bottom; ++Y)
    {
        *(uint32 *)Pixel = Color;
        Pixel += GlobalBackBuffer->Pitch;
    }
}

inline void
SDLDrawSoundBufferMarker(sdl_offscreen_buffer *Backbuffer,
                         sdl_sound_output *SoundOutput,
                         real32 C, int PadX, int Top, int Bottom,
                         int Value, uint32 Color)
{
    Assert(Value < SoundOutput->SecondaryBufferSize);
    real32 XReal32 = (C * (real32)Value);
    int X = PadX + (int)XReal32;
    SDLDebugDrawVertical(Backbuffer, X, Top, Bottom, Color);
}

internal void
SDLDebugSyncDisplay(sdl_offscreen_buffer *Backbuffer,
                    int MarkerCount, sdl_debug_time_marker *Markers,
                    sdl_sound_output *SoundOutput, real32 TargetSecondsPerFrame)
{
    int PadX = 16;
    int PadY = 16;

    int Top = PadY;
    int Bottom = Backbuffer->Height - PadY;

    real32 C = (real32)(Backbuffer->Width - 2*PadX) / (real32)SoundOutput->SecondaryBufferSize;
    for(int MarkerIndex = 0; MarkerIndex < MarkerCount; ++MarkerIndex)
    {
        sdl_debug_time_marker *ThisMarker = &Markers[MarkerIndex];
        SDLDrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->PlayCursor, 0xFFFFFFFF);
        SDLDrawSoundBufferMarker(Backbuffer, SoundOutput, C, PadX, Top, Bottom, ThisMarker->WriteCursor, 0xFFFF0000);
    }
}

// ENTER HERE
int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC | SDL_INIT_AUDIO);
    uint64 PerfCountFrequency = SDL_GetPerformanceFrequency();

    // Initialize game controllers:
    SDLOpenGameControllers();
    // create the window
    SDL_Window* Window = SDL_CreateWindow("Handmade Hero",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          640,
                                          480,
                                          SDL_WINDOW_RESIZABLE);

    if (Window) {
        // create a renderer for the window
        SDL_Renderer* Renderer = SDL_CreateRenderer(Window, -1, SDL_RENDERER_PRESENTVSYNC);

        printf("Refresh rate is %d Hz\n", SDLGetWindowRefreshRate(Window));
        int GameUpdateHz = 30;
        real32 TargetSecondsPerFrame = 1.0f / (real32) GameUpdateHz;

        if (Renderer) {
            bool Running = true;
            sdl_window_dimension Dimension = SDLGetWindowDimension(Window);
            SDLResizeTexture(&GlobalBackbuffer, Renderer, Dimension.Width, Dimension.Height);

            game_input Input[2] = {};
            game_input* NewInput = &Input[0];
            game_input* OldInput = &Input[1];

            sdl_sound_output SoundOutput = {};
            SoundOutput.SamplesPerSecond = 48000;
            SoundOutput.RunningSampleIndex = 0;
            SoundOutput.BytesPerSample = sizeof(int16) * 2;
            SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
            SoundOutput.tSine = 0.0f;
            SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;
            // Open audio device
            SDLInitAudio(48000, SoundOutput.SecondaryBufferSize);
            // NOTE: calloc() allocates memory and clears it to zero.
            // It accepts the number of things being allocated and their size.
            int16* Samples = (int16*) calloc(SoundOutput.SamplesPerSecond, SoundOutput.BytesPerSample);
            SDL_PauseAudio(0);

#if HANDMADE_INTERNAL
            void* BaseAddress = (void*) Terabytes(2);
#else
            void *BaseAddress = (void *)(0);
#endif

            game_memory GameMemory = {};
            GameMemory.PermanentStorageSize = Megabytes(64);
            GameMemory.TransientStorageSize = Gigabytes(4);

            uint64 TotalStorageSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;

            GameMemory.PermanentStorage = mmap(BaseAddress, TotalStorageSize,
                                               PROT_READ | PROT_WRITE,
                                               MAP_ANON | MAP_PRIVATE,
                                               -1, 0);

            Assert(GameMemory.PermanentStorage);

            GameMemory.TransientStorage = (uint8*) (GameMemory.PermanentStorage) + GameMemory.PermanentStorageSize;

            int DebugTimeMarkerIndex = 0;
            sdl_debug_time_marker DebugTimeMarkers[GameUpdateHz / 2] = {0};

            uint64 LastCounter = SDL_GetPerformanceCounter();
            uint64 LastCycleCount = _rdtsc();
            while (Running) {
                game_controller_input* OldKeyboardController = GetController(OldInput, 0);
                game_controller_input* NewKeyboardController = GetController(NewInput, 0);
                *NewKeyboardController = {};
                for (int ButtonIndex = 0; ButtonIndex < ArrayCount(NewKeyboardController->Buttons); ++ButtonIndex) {
                    NewKeyboardController->Buttons[ButtonIndex].EndedDown =
                            OldKeyboardController->Buttons[ButtonIndex].EndedDown;
                }

                SDL_Event Event;
                while (SDL_PollEvent(&Event)) {
                    if (HandleEvent(&Event, NewKeyboardController)) {
                        Running = false;
                    }
                }

                // poll controllers for input
                for (int ControllerIndex = 0; ControllerIndex < MAX_CONTROLLERS; ++ControllerIndex) {
                    if (ControllerHandles[ControllerIndex] != 0 &&
                        SDL_GameControllerGetAttached(ControllerHandles[ControllerIndex])) {
                        game_controller_input* OldController = GetController(OldInput, ControllerIndex + 1);
                        game_controller_input* NewController = GetController(NewInput, ControllerIndex + 1);

                        NewController->IsConnected = true;

                        // NOTE: do something with the DPad, Start and Selected?
                        bool Up = SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                              SDL_CONTROLLER_BUTTON_DPAD_UP);
                        bool Down = SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                SDL_CONTROLLER_BUTTON_DPAD_DOWN);
                        bool Left = SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                SDL_CONTROLLER_BUTTON_DPAD_LEFT);
                        bool Right = SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                 SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
                        bool Start = SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                 SDL_CONTROLLER_BUTTON_START);
                        bool Back = SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                SDL_CONTROLLER_BUTTON_BACK);

                        SDLProcessGameControllerButton(&(OldController->LeftShoulder),
                                                       &(NewController->LeftShoulder),
                                                       SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                                   SDL_CONTROLLER_BUTTON_LEFTSHOULDER));

                        SDLProcessGameControllerButton(&(OldController->RightShoulder),
                                                       &(NewController->RightShoulder),
                                                       SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                                   SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));

                        SDLProcessGameControllerButton(&(OldController->ActionDown),
                                                       &(NewController->ActionDown),
                                                       SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                                   SDL_CONTROLLER_BUTTON_A));

                        SDLProcessGameControllerButton(&(OldController->ActionRight),
                                                       &(NewController->ActionRight),
                                                       SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                                   SDL_CONTROLLER_BUTTON_B));

                        SDLProcessGameControllerButton(&(OldController->ActionLeft),
                                                       &(NewController->ActionLeft),
                                                       SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                                   SDL_CONTROLLER_BUTTON_X));

                        SDLProcessGameControllerButton(&(OldController->ActionUp),
                                                       &(NewController->ActionUp),
                                                       SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                                                   SDL_CONTROLLER_BUTTON_Y));

                        NewController->StickAverageX = SDLProcessGameControllerAxisValue(
                                SDL_GameControllerGetAxis(ControllerHandles[ControllerIndex],
                                                          SDL_CONTROLLER_AXIS_LEFTX), 1);
                        NewController->StickAverageY = -SDLProcessGameControllerAxisValue(
                                SDL_GameControllerGetAxis(ControllerHandles[ControllerIndex],
                                                          SDL_CONTROLLER_AXIS_LEFTY), 1);

                        if ((NewController->StickAverageX != 0.0f) ||
                            (NewController->StickAverageY != 0.0f)) {
                            NewController->IsAnalog = true;
                        }
                        if (SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                        SDL_CONTROLLER_BUTTON_DPAD_UP)) {
                            NewController->StickAverageY = 1.0f;
                            NewController->IsAnalog = false;
                        }
                        if (SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                        SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
                            NewController->StickAverageY = -1.0f;
                            NewController->IsAnalog = false;
                        }
                        if (SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                        SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
                            NewController->StickAverageX = -1.0f;
                            NewController->IsAnalog = false;
                        }
                        if (SDL_GameControllerGetButton(ControllerHandles[ControllerIndex],
                                                        SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
                            NewController->StickAverageX = 1.0f;
                            NewController->IsAnalog = false;
                        }

                        real32 Threshold = 0.5f;

                        SDLProcessGameControllerButton(&(OldController->MoveLeft), &(NewController->MoveLeft),
                                                       NewController->StickAverageX < -Threshold);
                        SDLProcessGameControllerButton(&(OldController->MoveRight), &(NewController->MoveRight),
                                                       NewController->StickAverageX > Threshold);
                        SDLProcessGameControllerButton(&(OldController->MoveUp), &(NewController->MoveUp),
                                                       NewController->StickAverageY < -Threshold);
                        SDLProcessGameControllerButton(&(OldController->MoveDown), &(NewController->MoveDown),
                                                       NewController->StickAverageY > Threshold);
                    } else {
                        // TODO: controller not plugged in
                    }
                }

                // AUDIO TEST
                SDL_LockAudio();
                int ByteToLock =
                        (SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) % SoundOutput.SecondaryBufferSize;
                int TargetCursor = ((AudioRingBuffer.PlayCursor +
                                     (SoundOutput.LatencySampleCount * SoundOutput.BytesPerSample)) %
                                    SoundOutput.SecondaryBufferSize);
                int BytesToWrite;
                if (ByteToLock > TargetCursor) {
                    BytesToWrite = (SoundOutput.SecondaryBufferSize - ByteToLock);
                    BytesToWrite += TargetCursor;
                } else {
                    BytesToWrite = TargetCursor - ByteToLock;
                }

                SDL_UnlockAudio();

                game_sound_output_buffer SoundBuffer = {};
                SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
                SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                SoundBuffer.Samples = Samples;

                game_offscreen_buffer Buffer = {};
                Buffer.Memory = GlobalBackbuffer.Memory;
                Buffer.Width = GlobalBackbuffer.Width;
                Buffer.Height = GlobalBackbuffer.Height;
                Buffer.Pitch = GlobalBackbuffer.Pitch;
                GameUpdateAndRender(&GameMemory, NewInput, &Buffer, &SoundBuffer);

                game_input* Temp = NewInput;
                NewInput = OldInput;
                OldInput = Temp;

                SDLFillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);

                if (SDLGetSecondsElapsed(LastCounter, SDL_GetPerformanceCounter()) < TargetSecondsPerFrame) {
                    int32 TimeToSleep =
                            ((TargetSecondsPerFrame - SDLGetSecondsElapsed(LastCounter, SDL_GetPerformanceCounter())) *
                             1000) - 1;
                    if (TimeToSleep > 0) {
                        SDL_Delay(TimeToSleep);
                    }
                    Assert(SDLGetSecondsElapsed(LastCounter, SDL_GetPerformanceCounter()) < TargetSecondsPerFrame);
                    while (SDLGetSecondsElapsed(LastCounter, SDL_GetPerformanceCounter()) < TargetSecondsPerFrame) {
                        // waiting...
                    }
                }

                uint64 EndCounter = SDL_GetPerformanceCounter();

#if HANDMADE_INTERNAL
                SDLDebugSyncDisplay(&GlobalBackbuffer, ArrayCount(DebugTimeMarkers), DebugTimeMarkers,
                                    &SoundOutput, TargetSecondsPerFrame);
#endif

                SDLUpdateWindow(Window, Renderer, &GlobalBackbuffer);

#if HANDMADE_INTERNAL
                // this is debug code
                {
                    sdl_debug_time_marker *Marker = &DebugTimeMarkers[DebugTimeMarkerIndex++];
                    if(DebugTimeMarkerIndex > ArrayCount(DebugTimeMarkers))
                    {
                        DebugTimeMarkerIndex = 0;
                    }
                    Marker->PlayCursor = AudioRingBuffer.PlayCursor;
                    Marker->WriteCursor = AudioRingBuffer.WriteCursor;
                }
#endif

                uint64 EndCycleCount = _rdtsc();
                uint64 CounterElapsed = EndCounter - LastCounter;
                uint64 CyclesElapsed = EndCycleCount - LastCycleCount;

                real64 MSPerFrame = (((1000.0f * (real64) CounterElapsed) / (real64) PerfCountFrequency));
                real64 FPS = (real64) PerfCountFrequency / (real64) CounterElapsed;
                real64 MCPF = ((real64) CyclesElapsed / (1000.0f * 1000.0f));

                printf("%.02fms/f, %.02ff/s, %.02fMHz/f\n", MSPerFrame, FPS, MCPF);

                LastCycleCount = EndCycleCount;
                LastCounter = EndCounter;
            }
        } else {
            // TODO: logging
        }
    } else {
        // TODO: logging
    }

    SDLCloseGameControllers();
    SDL_Quit();
    return (0);
}