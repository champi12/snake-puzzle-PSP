/* audio.h - efectos (data/sfx.bin) y musica (data/music*.pcm, PCM 22050 Hz mono leido a trozos) */
#ifndef AUDIO_H
#define AUDIO_H

/* mismo orden que SFX en tools/extract.py */
enum {
    SFX_WORM, SFX_EAT, SFX_STOP, SFX_THORN, SFX_DROP, SFX_MOVE_STONE, SFX_STONE_DROP, SFX_WIN, SFX_LOSE, SFX_CLICK,
    SFX_CLICK8, SFX_PROGRESS, SFX_REWARD, SFX_UNLOCK, SFX_COUNT
};

int audio_init(const char *dir);
void audio_shutdown(void);
void audio_play(int id, float vol);
/* 0 normal, 1 Ano Nuevo Lunar */
void audio_music(int which);
void audio_set_enabled(int sfx, int music);
const char *audio_status(void);   /* para el medidor de SELECT */

#endif
