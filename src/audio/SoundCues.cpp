#include "SoundCues.h"

#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

void playTones(SoundCues::Cue cue)
{
    switch (cue) {
    case SoundCues::Cue::Start: // dos tonos ascendentes
        Beep(880, 80);
        Beep(1319, 120);
        break;
    case SoundCues::Cue::Stop: // dos tonos descendentes
        Beep(1319, 80);
        Beep(880, 140);
        break;
    case SoundCues::Cue::Clip: // tres notas cortas
        Beep(1047, 70);
        Beep(1047, 70);
        Beep(1319, 110);
        break;
    case SoundCues::Cue::Error: // tonos graves
        Beep(330, 200);
        Beep(247, 300);
        break;
    }
}

} // namespace

namespace SoundCues {

void play(Cue cue, bool wait)
{
    if (wait)
        playTones(cue);
    else
        std::thread([cue] { playTones(cue); }).detach();
}

} // namespace SoundCues
