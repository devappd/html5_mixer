/*
  SDL_mixer:  An audio mixer library based on the SDL library
  Copyright (C) 1997-2021 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* This file supports an external command for playing music */

#include "music_html5.h"

#ifdef MUSIC_HTML5

#include <emscripten.h>

typedef struct {
    int id;
    SDL_RWops *src;
    SDL_bool freesrc;
} MusicHTML5;

static SDL_bool html5_opened(void)
{
    return EM_ASM_INT({
        return !!Module["SDL2Mixer"] && !!Module["SDL2Mixer"].music;
    });
}

static int MusicHTML5_Open(const SDL_AudioSpec *spec)
{
    (void)spec;

    if (html5_opened())
        return 0;

    EM_ASM(({
        Module["SDL2Mixer"] = {
            blob: {
                // URL.createObjectURL(...): numUses (int)
            },

            music: {
                // randomId: new Audio(file);
            },

            deleteMusic: function(id) {
                if (!(id in this.music))
                    return;

                const url = this.music[id].currentSrc;

                this.music[id].pause();
                this.music[id].removeAttribute("src");
                this.music[id].load();
                this.music[id].remove();

                delete this.music[id];

                this.deleteBlob(url);
            },

            deleteBlob: function(url) {
                if (url in this.blob && --this.blob[url] <= 0) {
                    URL.revokeObjectURL(url);
                    delete this.blob[url];
                }
            },

            getNewId: function() {
                const min = 0;
                const max = 2147483647; // INT32_MAX

                // Guard against collisions
                let id;
                do
                {
                    id = Math.floor(Math.random() * (max - min + 1) + min);
                } while(id in this.music);
                return id;
            },

            canPlayType: function(type) {
                let audio;

                // Allow user to create shortcuts, i.e. just "mp3"
                const formats = {
                    mp3: 'audio/mpeg',
                    ogg: 'audio/ogg',
                    wav: 'audio/wav',
                    flac: 'audio/flac',
                    mp4: 'audio/mp4',
                    m4a: 'audio/mp4',
                    aif: 'audio/x-aiff',
                    webm: 'audio/webm',
                    adts: 'audio/aac'
                };

                // Get any <audio>, doesn't matter which
                for (const prop in this.music) {
                    if (this.music[prop] instanceof HTMLMediaElement) {
                        audio = this.music[prop];
                        break;
                    }
                }

                if(!audio)
                    audio = this.music[0] = new Audio();

                return audio.canPlayType(formats[type] || type);
            },

            canPlayFile: function(file) {
                const type = file.split('.').pop();
                if (type)
                    return this.canPlayType(type);
                else // Fail without Exception
                    return false;
            },

            musicFinished: function(e) {
                const audio = e.target;

                if (!(audio instanceof HTMLMediaElement))
                    return;

                // if playCount == -1, then audio.loop is true and the
                // "ended" event is not fired (i.e., we never reach this function.)

                if (--audio.dataset.playCount > 0) {
                    audio.currentTime = 0;
                    audio.play();
                } else {
                    audio.dataset.playCount = 0;
                    audio.currentTime = 0;
                    audio.loop = false;

                    // Signal `true` to run hookMusicFinished
                    return true;
                }
            },

            musicError: function(e) {
                const audio = e.target;

                if (!(audio instanceof HTMLMediaElement))
                    return;

                console.error("Error " + audio.error.code + "; details: " + audio.error.message);

                // Reset to defaults
                audio.pause();
                audio.dataset.playCount = 0;
                audio.currentTime = 0;
                audio.loop = false;
            }
        };
    }));

#ifdef HTML5_MIXER
    // HookMusicFinished support for html5_mixer (a minimal implementation of SDL2_mixer)
    EM_ASM({
        const hookMusicFinished = $0;
        Module["SDL2Mixer"].musicFinished = function(predefined) {
            return function(e) {
                if (predefined(e))
                    dynCall("v", hookMusicFinished, []);
            }
        }(Module["SDL2Mixer"].musicFinished);
    }, run_music_finished_hook);
#endif

    return 0;
}

static void *MusicHTML5_CreateFromRW(SDL_RWops *src, int freesrc)
{
    void *buf;
    int id = -1;
    int size = src->size(src);
    MusicHTML5 *music;

    if (src->type == SDL_RWOPS_STDFILE)
        buf = src->hidden.stdio.fp;
    else if (src->type == SDL_RWOPS_MEMORY || src->type == SDL_RWOPS_MEMORY_RO)
        buf = src->hidden.mem.base;
    else
    {
        Mix_SetError("Unsupported RWops type: %d", src->type);
        if (freesrc)
            src->close(src);
        return NULL;
    }

    if (buf && size > 0)
    {
        id = EM_ASM_INT({
            const ptr = $0;
            const size = $1;

            const arr = new Uint8Array(Module.HEAPU8.buffer, ptr, size);
            const blob = new Blob([arr], { type: "octet/stream" });
            const url = URL.createObjectURL(blob);

            // TODO: Match blob by ptr and size so we don't duplicate

            if (!(url in Module["SDL2Mixer"].blob))
                Module["SDL2Mixer"].blob[url] = 0;
            Module["SDL2Mixer"].blob[url]++;

            const id = Module["SDL2Mixer"].getNewId();
            Module["SDL2Mixer"].music[id] = new Audio(url);
            Module["SDL2Mixer"].music[id].addEventListener("ended", Module["SDL2Mixer"].musicFinished, false);
            Module["SDL2Mixer"].music[id].addEventListener("error", Module["SDL2Mixer"].musicError, false);
            return id;
        }, buf, size);
    }

    if (id == -1)
        return NULL;

    /* Allocate and fill the music structure */
    music = (MusicHTML5 *)SDL_calloc(1, sizeof *music);
    if (music == NULL) {
        Mix_SetError("Out of memory");
        return NULL;
    }
    music->id = id;
    music->src = src;
    music->freesrc = freesrc;

    /* We're done */
    return music;
}

/* Load a music stream from the given file */
static void *MusicHTML5_CreateFromFile(const char *file)
{
    MusicHTML5 *music;
    int id = -1;

    SDL_RWops *src = SDL_RWFromFile(file, "rb");
    if (src != NULL)
    {
#ifdef HTML5_MIXER
        // Just pass the entire path; we don't bundle detect_music_type()
        const char *fileType = file;
#else
        Mix_MusicType type = detect_music_type(src);
        
        char *fileType;
        switch(type) {
            case MUS_OGG:
                fileType = "ogg";
                break;

            case MUS_FLAC:
                fileType = "flac";
                break;

            case MUS_MP3:
                fileType = "mp3";
                break;

            default:
                fileType = file;
                break;
        }
#endif

        SDL_bool canPlay = EM_ASM_INT({
            const file = UTF8ToString($0);
            return Module["SDL2Mixer"].canPlayFile(file);
        }, file);

        if (canPlay)
            return MusicHTML5_CreateFromRW(src, SDL_TRUE);
        else
        {
            Mix_SetError("Format is not supported by HTML5 <audio>");
            src->close(src);
            return NULL;
        }
    }
    else
    {
        // Assume it's a URL
        id = EM_ASM_INT({
            function isValidUrl(string) {
                let url;
                
                try {
                    url = new URL(string);
                } catch (_) {
                    return false;  
                }

                return url.protocol === "http:" || url.protocol === "https:";
            }

            const url = UTF8ToString($0);

            if (!isValidUrl) {
                console.error(`URL ${url} is invalid`);
                return -1;
            }

            const id = Module["SDL2Mixer"].getNewId();
            Module["SDL2Mixer"].music[id] = new Audio(url);
            Module["SDL2Mixer"].music[id].addEventListener("ended", Module["SDL2Mixer"].musicFinished, false);
            Module["SDL2Mixer"].music[id].addEventListener("error", Module["SDL2Mixer"].musicError, false);
            return id;
        }, file);
    }

    if (id == -1)
        return NULL;

    /* Allocate and fill the music structure */
    music = (MusicHTML5 *)SDL_calloc(1, sizeof *music);
    if (music == NULL) {
        Mix_SetError("Out of memory");
        return NULL;
    }
    music->id = id;
    music->freesrc = SDL_FALSE;

    /* We're done */
    return music;
}

/* Set the volume for a given music stream */
static void MusicHTML5_SetVolume(void *context, int volume)
{
    MusicHTML5 *music = (MusicHTML5 *)context;
    volume /= MIX_MAX_VOLUME;

    EM_ASM({
        const id = $0;
        const volume = Math.min(Math.max(0, $1), 1);
        Module["SDL2Mixer"].music[id].volume = volume;
    }, music->id, volume);
}

static void MusicHTML5_Stop(void *context);

/* Start playback of a given music stream */
static int MusicHTML5_Play(void *context, int play_count)
{
    MusicHTML5 *music = (MusicHTML5 *)context;

    if (play_count <= 0 && play_count != -1)
    {
        // do not play but do not throw errors either.
        // TODO: What is SDL Mixer's state when passing play_count <= 0?
        MusicHTML5_Stop(context);
        return 0;
    }
    int status = EM_ASM_INT({
        try {
            const id = $0;
            const playCount = $1;

            // Retain play_count for handling in musicFinished()
            Module["SDL2Mixer"].music[id].dataset.playCount = playCount;

            // If play_count == -1, we are looping
            Module["SDL2Mixer"].music[id].loop = (playCount == -1);

            // TODO: Asyncify Promise
            const played = Module["SDL2Mixer"].music[id].play();

            // Older browsers do not return a Promise
            if (played)
                played.catch((e) => console.error(e));
        } catch (e) {
            console.error(e);
            return -1;
        }
        return 0;
    }, music->id, play_count);

    if (status < 0)
        Mix_SetError("Emscripten HTML5 error, see developer console.");
    
    return status;
}

/* Return non-zero if a stream is currently playing */
static SDL_bool MusicHTML5_IsPlaying(void *context)
{
    MusicHTML5 *music = (MusicHTML5 *)context;
    
    return EM_ASM_INT({
        const id = $0;
        return  Module["SDL2Mixer"].music[id]
            &&  Module["SDL2Mixer"].music[id].currentTime > 0
            // SDL Mixer considers "paused" music as "playing"
            //&& !Module["SDL2Mixer"].music[id].paused
            && !Module["SDL2Mixer"].music[id].ended
            &&  Module["SDL2Mixer"].music[id].readyState > 2;
    }, music->id);
}

/* Jump (seek) to a given position (time is in seconds) */
static int MusicHTML5_Seek(void *context, double time)
{
    MusicHTML5 *music = (MusicHTML5 *)context;

    EM_ASM({
        const id = $0;
        const time = $1;
        Module["SDL2Mixer"].music[id].currentTime = time;
    }, music->id, time);

    return 0;
}

/* Pause playback of a given music stream */
static void MusicHTML5_Pause(void *context)
{
    MusicHTML5 *music = (MusicHTML5 *)context;

    EM_ASM({
        const id = $0;
        Module["SDL2Mixer"].music[id].pause();
    }, music->id);
}

/* Resume playback of a given music stream */
static void MusicHTML5_Resume(void *context)
{
    MusicHTML5 *music = (MusicHTML5 *)context;

    EM_ASM({
        const id = $0;
        Module["SDL2Mixer"].music[id].play();
    }, music->id);
}

/* Stop playback of a stream previously started with MusicHTML5_Start() */
static void MusicHTML5_Stop(void *context)
{
    MusicHTML5 *music = (MusicHTML5 *)context;

    EM_ASM({
        const id = $0;

        Module["SDL2Mixer"].music[id].pause();
        Module["SDL2Mixer"].music[id].dataset.playCount = 0;
        Module["SDL2Mixer"].music[id].currentTime = 0;
        Module["SDL2Mixer"].music[id].loop = false;
    }, music->id);
}

/* Close the given music stream */
static void MusicHTML5_Delete(void *context)
{
    MusicHTML5 *music = (MusicHTML5 *)context;

    if (html5_opened()) {
        EM_ASM({
            const id = $0;
            if (id in Module["SDL2Mixer"].music)
                Module["SDL2Mixer"].deleteMusic(id);
        }, music->id);
    }

    if (music->freesrc && music->src)
        SDL_RWclose(music->src);

    SDL_free(music);
}

static void MusicHTML5_Close(void)
{
    if (!html5_opened())
        return;

    EM_ASM({
        for(const prop in Module["SDL2Mixer"].music) {
            if (!(Module["SDL2Mixer"].music[prop] instanceof HTMLMediaElement))
                continue;
            Module["SDL2Mixer"].deleteMusic(prop);
        }
        delete Module["SDL2Mixer"];
    });
}

Mix_MusicInterface Mix_MusicInterface_HTML5 =
{
    "HTML5",
    MIX_MUSIC_HTML5,
    MUS_HTML5,
    SDL_FALSE,
    SDL_FALSE,

    NULL,   /* Load */
    MusicHTML5_Open,
    MusicHTML5_CreateFromRW,
    MusicHTML5_CreateFromFile,
    MusicHTML5_SetVolume,
    MusicHTML5_Play,
    MusicHTML5_IsPlaying,
    NULL,   /* GetAudio */
    MusicHTML5_Seek,
    MusicHTML5_Pause,
    MusicHTML5_Resume,
    MusicHTML5_Stop,
    MusicHTML5_Delete,
    MusicHTML5_Close,
    NULL,   /* Unload */
};

#endif /* MUSIC_HTML5 */

/* vi: set ts=4 sw=4 expandtab: */
