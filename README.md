# html5_mixer

This is a minimal drop-in replacement for the music functions of `SDL2_mixer` to be used with the [Emscripten compiler](https://emscripten.org). It uses HTML5 `<audio>` to render music.

## Overview

[`emscripten-ports/SDL2_mixer`](https://github.com/emscripten-ports/SDL2_mixer) is a WebAssembly port
that performs all sound decoding in WASM. It pushes samples via [`emscripten-ports/SDL2`](https://github.com/emscripten-ports/SDL2)
to a [Web Audio](https://developer.mozilla.org/en-US/docs/Web/API/Web_Audio_API) context for playback.

While this approach ensures the highest parity with native builds, the program is susceptible to
dropped samples and playback stutters because music decoding and audio rendering both occur in the main thread.

To reduce processing load, we implement the `SDL2_mixer` interface to use the browser's native
playback features via
[`<audio>`](https://developer.mozilla.org/en-US/docs/Web/HTML/Element/audio).

## How to Use

Download this package to a location of your choice.
Specify `./include` in your "Include" directories and specify the files within `./src` in your sources.

In your compiler and linker flags, specify `-s USE_SDL=2`. You may use this library concurrently
with SDL Mixer (`-s USE_SDL_MIXER=2`), but this is not required.

You may shim SDL Mixer's `Mix_*()` music functions by specifying `-DHTML5_MIXER_SHIM_MUSIC` in your macro defines.

Support exists to link this library without SDL2, but this is untested. If you wish to try, specify
`-DHTML5_MIXER_NO_SDL`.

## Notes

We do not perform any decoding; we merely pass the URL or data buffer to an `Audio()` instance.

Your audio files must be supported by the user's web browser. For a format compatibility table, see
[Wikipedia](https://en.wikipedia.org/wiki/HTML5_audio#Supported_audio_coding_formats).

Currently, we support a minimal subset of the `SDL2_mixer` API. See [issue #1](https://github.com/devappd/html5_mixer/issues/1)
for progress.

## Potential Next Steps

* Migrate SDL2's sound channel rendering from [`ScriptProcessorNode`](https://developer.mozilla.org/en-US/docs/Web/API/ScriptProcessorNode) to [`AudioWorklet`](https://developer.mozilla.org/en-US/docs/Web/API/AudioWorklet).
* Render music via `AudioContext.decodeAudioData()`. See:
    * [WebAudio/web-audio-api#1850](https://github.com/WebAudio/web-audio-api/issues/1850) -- SharedArrayBuffer source
    * [WebAudio/web-audio-api#337](https://github.com/WebAudio/web-audio-api/issues/337) -- Streaming partial content
    * [WebAudio/web-audio-api-v2#61](https://github.com/WebAudio/web-audio-api-v2/issues/61) -- Using WebCodec
    * [AnthumChris/fetch-stream-audio](https://github.com/AnthumChris/fetch-stream-audio)
    * [StackOverflow](https://stackoverflow.com/a/49386457)

## See Also

* [`SDL2_mixer` documentation](https://libsdl.org/projects/SDL_mixer/docs/index.html)
* [`SDL_mixer_html5`](https://github.com/devappd/SDL_mixer_html5) -- Full SDL Mixer with this HTML5 interface

## License

The `html5_mixer` code is released under MIT License.

This project incorporates code from [`SDL_mixer_html5`](https://github.com/devappd/SDL_mixer_html5) which is released under 3-clause BSD License.
