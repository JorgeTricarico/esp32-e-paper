#ifndef VOICE_BANK_H
#define VOICE_BANK_H

#include <Arduino.h>
#include "audio_es8311.h"

// Banco de frases pregrabadas en LittleFS (/bank/<name>.mp3).
// Para respuestas frecuentes sin gastar llamadas TTS ni internet.
// true = sonó el archivo. false = no existe/falló (usar chime de respaldo).
bool playBankPhrase(const char *name, AudioStreamCallback cb = nullptr);
bool bankHas(const char *name);

#endif
