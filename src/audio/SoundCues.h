#pragma once

// Avisos sonoros cortos para saber si se esta grabando.
// Se reproducen siempre FUERA de la grabacion: el de inicio antes de arrancar la captura y
// el de parada despues de cerrar los archivos, para que no queden en el audio grabado.
namespace SoundCues {

enum class Cue { Start, Stop, Error, Clip };

// wait = true bloquea unos 200 ms (se usa antes de empezar a grabar).
void play(Cue cue, bool wait);

} // namespace SoundCues
