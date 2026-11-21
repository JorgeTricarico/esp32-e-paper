#ifndef COPITO_TOOLS_H
#define COPITO_TOOLS_H

#include <Arduino.h>
#include "config.h"
#include "faces_bitmaps.h"

// Parsed agent tags from Gemini reply
struct CopitoTags {
    int drawType = 0;          // 0=none, 1=corazon 2=estrella 3=sol 4=nube 5=casa 6=musica 7=luna 8=libro 9=flor 10=rayo
    FaceState faceOverride = FACE_IDLE;
    bool hasFace = false;
    String rememberFact = "";
    String showText = "";      // [TEXT:...] texto corto para pantalla (la voz dice el resto)
    String sketchCommands = "";// [SKETCH:...] micro-DSL vectorial procedural para pantalla e-paper
    String mode = "";          // [MODE:RELOJ|CUENTO|CLIMA|DORMIDO|NORMAL] modo local post-respuesta
};

extern bool g_ntpSynced;

class CopitoTools {
public:
    // Parsea [DRAW:...] [FACE:...] [REMEMBER:...] [TEXT:...] [MODE:...] y los quita del texto
    static void parseTags(String &ioText, CopitoTags &out);
    // Reemplaza [TOOL:GET_TEMP] etc por valores reales de sensores
    static void resolveToolTags(String &ioText);
    // Generador de bocetos procedurales vectoriales Micro-DSL de alta fidelidad
    static void generateFallbackSketch(const String &query, String &outSketch);
    // Detección semántica de intención de dibujo (voz, texto, heurística)
    static bool hasDrawingIntent(const String &text);
    // Intento local SIN llamar a Gemini (texto del portal). true = respondido, ahorra API.
    static bool answerLocalText(const String &msg, String &outReply, int &outDraw);
    // Reloj: sincroniza NTP con redundancia multi-servidor (America/Argentina UTC-3).
    static void syncNtp();
    // Arma tarjeta de reloj "HH:MM + temp/hum" para pantalla. false = sin hora aun.
    static bool buildClockCard(String &outCard);
    // Mapeo nombre dibujo -> drawType
    static int drawNameToType(const String &name);
    // Mapeo nombre cara -> FaceState
    static bool faceNameToState(const String &name, FaceState &out);
};

#endif
