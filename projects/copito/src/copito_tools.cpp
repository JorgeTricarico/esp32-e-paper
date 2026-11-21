#include "copito_tools.h"
#include "sensor_shtc3.h"
#include "battery_monitor.h"
#include "rtc_pcf85063.h"
#include "copito_log.h"
#include <time.h>

int CopitoTools::drawNameToType(const String &name) {
    String n = name;
    n.toUpperCase();
    n.trim();
    if (n == "CORAZON" || n == "HEART" || n == "SONRISA" || n == "SMILE" || n == "AMOR") return 1;
    if (n == "ESTRELLA" || n == "STAR" || n == "MAGIA") return 2;
    if (n == "SOL" || n == "SUN" || n == "DIA") return 3;
    if (n == "NUBE" || n == "CLOUD" || n == "LLUVIA" || n == "CLIMA") return 4;
    if (n == "CASA" || n == "HOME" || n == "HOGAR") return 5;
    if (n == "MUSICA" || n == "MUSIC" || n == "NOTA" || n == "CANCION") return 6;
    if (n == "LUNA" || n == "MOON" || n == "NOCHE") return 7;
    if (n == "LIBRO" || n == "BOOK" || n == "CUENTO" || n == "ESCUELA") return 8;
    if (n == "FLOR" || n == "FLOWER" || n == "PLANTA") return 9;
    if (n == "RAYO" || n == "BOLT" || n == "TORMENTA" || n == "ENERGIA") return 10;
    return 0;
}

bool CopitoTools::faceNameToState(const String &name, FaceState &out) {
    String n = name;
    n.toUpperCase();
    n.trim();
    if (n == "FELIZ" || n == "HAPPY" || n == "ALEGRE" || n == "SONRISA") { out = FACE_HAPPY; return true; }
    if (n == "NORMAL" || n == "IDLE" || n == "NEUTRO") { out = FACE_IDLE; return true; }
    if (n == "ESCUCHANDO" || n == "LISTENING") { out = FACE_LISTENING; return true; }
    if (n == "PENSANDO" || n == "THINKING" || n == "PENSATIVO") { out = FACE_THINKING; return true; }
    if (n == "HABLANDO" || n == "SPEAKING") { out = FACE_SPEAKING; return true; }
    if (n == "DORMIDO" || n == "SLEEP" || n == "DORMILON") { out = FACE_SLEEP; return true; }
    if (n == "SORPRENDIDO" || n == "SORPRESA" || n == "GUINO" || n == "CURIOSO") { out = FACE_HAPPY; return true; }
    if (n == "TRISTE" || n == "ERROR" || n == "SAD") { out = FACE_ERROR; return true; }
    return false;
}

static bool extractBracketParam(const String &src, const String &prefix, String &outParam, int &outStart, int &outEnd) {
    int s = src.indexOf(prefix);
    if (s < 0) return false;
    int e = src.indexOf(']', s);
    if (e < 0) return false;
    outParam = src.substring(s + prefix.length(), e);
    outStart = s;
    outEnd = e;
    return true;
}

void CopitoTools::parseTags(String &ioText, CopitoTags &out) {
    out = CopitoTags();

    // --- [DRAW:XXX] (puede haber varios, nos quedamos con el primero valido) ---
    {
        String param; int s, e;
        // Buscar todos los DRAW y quedarnos con el primero
        int searchFrom = 0;
        while (true) {
            int found = ioText.indexOf("[DRAW:", searchFrom);
            if (found < 0) break;
            int close = ioText.indexOf(']', found);
            if (close < 0) break;
            String name = ioText.substring(found + 6, close);
            int t = drawNameToType(name);
            if (t > 0 && out.drawType == 0) out.drawType = t;
            // Quitar tag del texto
            ioText.remove(found, close - found + 1);
            searchFrom = found;
            if (searchFrom < 0) searchFrom = 0;
        }
        // Compat: [ICON:XXX] es alias de DRAW
        searchFrom = 0;
        while (true) {
            int found = ioText.indexOf("[ICON:", searchFrom);
            if (found < 0) break;
            int close = ioText.indexOf(']', found);
            if (close < 0) break;
            String name = ioText.substring(found + 6, close);
            int t = drawNameToType(name);
            if (t > 0 && out.drawType == 0) out.drawType = t;
            ioText.remove(found, close - found + 1);
            searchFrom = found;
        }
    }

    // --- [FACE:XXX] ---
    {
        String param; int s, e;
        if (extractBracketParam(ioText, "[FACE:", param, s, e)) {
            FaceState st;
            if (faceNameToState(param, st)) {
                out.faceOverride = st;
                out.hasFace = true;
            }
            ioText.remove(s, e - s + 1);
            // Si hay mas de uno, quitarlos tambien
            while (extractBracketParam(ioText, "[FACE:", param, s, e)) {
                ioText.remove(s, e - s + 1);
            }
        }
    }

    // --- [REMEMBER:...] memoria larga ---
    {
        String param; int s, e;
        if (extractBracketParam(ioText, "[REMEMBER:", param, s, e)) {
            param.trim();
            if (param.length() > 0 && param.length() < 200) {
                out.rememberFact = param;
            }
            ioText.remove(s, e - s + 1);
            // Quitar extras
            while (extractBracketParam(ioText, "[REMEMBER:", param, s, e)) {
                ioText.remove(s, e - s + 1);
            }
        }
    }

    // --- [TEXT:...] texto corto solo para pantalla (la voz dice el resto) ---
    {
        String param; int s, e;
        if (extractBracketParam(ioText, "[TEXT:", param, s, e)) {
            param.trim();
            if (param.length() > 0) {
                if (param.length() > 220) param = param.substring(0, 220);
                out.showText = param;
            }
            ioText.remove(s, e - s + 1);
            while (extractBracketParam(ioText, "[TEXT:", param, s, e)) {
                ioText.remove(s, e - s + 1);
            }
        }
    }

    // --- [SKETCH:...] micro-DSL vectorial procedural para pantalla ---
    {
        String param; int s, e;
        if (extractBracketParam(ioText, "[SKETCH:", param, s, e)) {
            param.trim();
            if (param.length() > 0) {
                out.sketchCommands = param;
            }
            ioText.remove(s, e - s + 1);
            while (extractBracketParam(ioText, "[SKETCH:", param, s, e)) {
                ioText.remove(s, e - s + 1);
            }
        }
    }

    // --- [MODE:...] modo local post-respuesta (RELOJ/CUENTO/CLIMA/DORMIDO/NORMAL) ---
    {
        String param; int s, e;
        if (extractBracketParam(ioText, "[MODE:", param, s, e)) {
            param.toUpperCase();
            param.trim();
            if (param == "RELOJ" || param == "CUENTO" || param == "CLIMA" || param == "DORMIDO") {
                out.mode = param;
            }
            // NORMAL o desconocido = sin modo (se ignora)
            ioText.remove(s, e - s + 1);
            while (extractBracketParam(ioText, "[MODE:", param, s, e)) {
                ioText.remove(s, e - s + 1);
            }
        }
    }

    // Heurística de respaldo si la IA no puso el tag exacto [MODE:...]
    if (out.mode.length() == 0) {
        String lower = ioText;
        lower.toLowerCase();
        if (lower.indexOf("modo reloj") >= 0 || lower.indexOf("pongo el reloj") >= 0 || lower.indexOf("activando el reloj") >= 0) {
            out.mode = "RELOJ";
        } else if (lower.indexOf("modo clima") >= 0) {
            out.mode = "CLIMA";
        }
    }

    // --- Resolver tools inline [TOOL:...] ---
    resolveToolTags(ioText);

    // --- Tags sin cerrar (respuesta truncada por tokens): un "[SKETCH: ..."
    // sin "]" ni se extrae ni se debe mostrar/hablar (2026-09-10: se vocalizó
    // "macro tri ciento cincuenta..."). Se amputa desde el prefijo.
    {
        static const char *kPrefixes[] = {
            "[DRAW:", "[SKETCH:", "[MODE:", "[TEXT:", "[TOOL:", "[FACE:",
            "[ICON:", "[REMEMBER:", "[draw:", "[sketch:", "[mode:", "[text:",
            "[tool:", "[face:", "[icon:", "[remember:",
        };
        for (size_t i = 0; i < sizeof(kPrefixes) / sizeof(kPrefixes[0]); i++) {
            int u = ioText.indexOf(kPrefixes[i]);
            while (u >= 0) {
                if (ioText.indexOf(']', u) < 0) {
                    ioText.remove(u); // sin cierre: todo el resto es markup
                    break;
                }
                u = ioText.indexOf(kPrefixes[i], u + 1);
            }
        }
    }

    ioText.trim();
}

bool g_ntpSynced = false;

void CopitoTools::resolveToolTags(String &ioText) {    // Valores reales (baratos, sin internet)
    char tempBuf[32] = "";
    char humBuf[32] = "";
    if (g_ambientSensorValid) {
        snprintf(tempBuf, sizeof(tempBuf), "%.1f grados", (double)g_ambientTempC);
        snprintf(humBuf, sizeof(humBuf), "%d por ciento", (int)round(g_ambientHum));
    } else if (envSensor.isValid()) {
        snprintf(tempBuf, sizeof(tempBuf), "%.1f grados", (double)envSensor.getTemperature());
        snprintf(humBuf, sizeof(humBuf), "%d por ciento", (int)round(envSensor.getHumidity()));
    } else {
        snprintf(tempBuf, sizeof(tempBuf), "no pude leer el sensor ahora");
        snprintf(humBuf, sizeof(humBuf), "no pude leer la humedad ahora");
    }

    char batBuf[64];
    batteryMonitor.getStatusText(batBuf, sizeof(batBuf));

    char horaBuf[64];
    struct tm tm;
    bool hasTime = getLocalTime(&tm, 50);
    if (!hasTime) {
        hasTime = rtcDriver.readTime(tm);
    }
    if (hasTime && tm.tm_year >= 120 && (g_ntpSynced || tm.tm_hour > 0 || tm.tm_min > 0)) {
        snprintf(horaBuf, sizeof(horaBuf), "son las %02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        snprintf(horaBuf, sizeof(horaBuf), "mi reloj se esta sincronizando con internet en este momento");
    }

    ioText.replace("[TOOL:GET_TEMP]", String(tempBuf));
    ioText.replace("[TOOL:GET_HUM]", String(humBuf));
    ioText.replace("[TOOL:GET_TEMPERATURA]", String(tempBuf));
    ioText.replace("[TOOL:GET_HUMEDAD]", String(humBuf));
    ioText.replace("[TOOL:GET_BATERIA]", String(batBuf));
    ioText.replace("[TOOL:GET_BATTERY]", String(batBuf));
    ioText.replace("[TOOL:GET_HORA]", String(horaBuf));
}

bool CopitoTools::hasDrawingIntent(const String &text) {
    if (text.length() == 0) return false;
    String t = text;
    t.toLowerCase();
    t.replace("á", "a"); t.replace("é", "e"); t.replace("í", "i");
    t.replace("ó", "o"); t.replace("ú", "u"); t.replace("ñ", "n");

    // 1. Verbos de dibujo directos y variantes
    if (t.indexOf("dibuja") >= 0 || t.indexOf("dibujo") >= 0 || t.indexOf("dibujar") >= 0 ||
        t.indexOf("ilustra") >= 0 || t.indexOf("ilustrame") >= 0 || t.indexOf("boceto") >= 0 ||
        t.indexOf("grafica") >= 0 || t.indexOf("pinta") >= 0 || t.indexOf("pintame") >= 0 ||
        t.indexOf("sketch") >= 0) {
        return true;
    }

    // 2. Verbos de petición con artículos o expresiones directas
    if (t.indexOf("hazme un") >= 0 || t.indexOf("hazme una") >= 0 ||
        t.indexOf("haceme un") >= 0 || t.indexOf("haceme una") >= 0 ||
        t.indexOf("haz un") >= 0 || t.indexOf("haz una") >= 0 ||
        t.indexOf("hace un") >= 0 || t.indexOf("hace una") >= 0 ||
        t.indexOf("creame un") >= 0 || t.indexOf("creame una") >= 0 ||
        t.indexOf("crea un") >= 0 || t.indexOf("crea una") >= 0 ||
        t.indexOf("mostrame un") >= 0 || t.indexOf("mostrame una") >= 0 ||
        t.indexOf("muestra un") >= 0 || t.indexOf("muestra una") >= 0 ||
        t.indexOf("ponme un") >= 0 || t.indexOf("ponme una") >= 0 ||
        t.indexOf("pone un") >= 0 || t.indexOf("pone una") >= 0 ||
        t.indexOf("pon un") >= 0 || t.indexOf("pon una") >= 0 ||
        t.indexOf("dame un") >= 0 || t.indexOf("dame una") >= 0 ||
        t.indexOf("dame el") >= 0 || t.indexOf("dame la") >= 0 ||
        t.indexOf("quiero ver") >= 0 || t.indexOf("quiero un") >= 0 || t.indexOf("quiero una") >= 0 ||
        t.indexOf("podrias dibujar") >= 0 || t.indexOf("puedes dibujar") >= 0 ||
        t.indexOf("me dibujas") >= 0 || t.indexOf("me haces") >= 0 || t.indexOf("me haces un") >= 0 ||
        t.indexOf("un nino") >= 0 || t.indexOf("una nina") >= 0 || t.indexOf("un nene") >= 0 ||
        t.indexOf("un chico") >= 0 || t.indexOf("un pibe") >= 0 || t.indexOf("jugando") >= 0 ||
        t.indexOf("un dragon") >= 0 || t.indexOf("un dinosaurio") >= 0 || t.indexOf("un astronauta") >= 0) {
        return true;
    }

    // 3. Mención explícita a la pantalla o display
    if (t.indexOf("en pantalla") >= 0 || t.indexOf("en el display") >= 0 ||
        t.indexOf("en el epaper") >= 0 || t.indexOf("en la pantalla") >= 0) {
        return true;
    }

    // 4. Conteo de sustantivos visuales (escenas compuestas "X con Y", "X y Z")
    int visualNounCount = 0;
    const char *nouns[] = {
        "vaca", "vaquita", "casa", "casita", "sol", "solcito", "luna", "gato", "gatito", "michi",
        "perro", "perrito", "arbol", "arbolito", "flor", "florcita", "cohete", "auto", "autito",
        "coche", "barco", "velero", "avion", "kraken", "pulpo", "edificio", "iglesia", "castillo",
        "montana", "volcan", "espada", "escudo", "corazon", "estrella", "cafe", "robot", "mandala",
        "nino", "nina", "nene", "chico", "pibe", "pelota", "dragon", "astronauta"
    };
    for (size_t i = 0; i < sizeof(nouns)/sizeof(nouns[0]); i++) {
        if (t.indexOf(nouns[i]) >= 0) {
            visualNounCount++;
        }
    }
    if (visualNounCount >= 2) {
        return true; // Ej: "casita con sol y vaca", "luna y estrellas", "nino jugando con pelota"
    }

    // 5. Sustantivos icónicos que por sí solos en contexto de petición implican visual
    if (t.indexOf("kraken") >= 0 || t.indexOf("pulpo") >= 0 ||
        t.indexOf("edificio") >= 0 || t.indexOf("iglesia") >= 0 ||
        t.indexOf("cohete") >= 0 || t.indexOf("barco") >= 0 ||
        t.indexOf("velero") >= 0 || t.indexOf("avion") >= 0 ||
        t.indexOf("espada") >= 0 || t.indexOf("escudo") >= 0 ||
        t.indexOf("luna sonriente") >= 0 || t.indexOf("luna son dientes") >= 0 ||
        t.indexOf("nino jugando") >= 0 || t.indexOf("nene jugando") >= 0) {
        return true;
    }

    // 6. Petición que empieza con "dame " o "pon " y tiene al menos un sustantivo visual
    if ((t.indexOf("dame ") >= 0 || t.indexOf("pon ") >= 0 || t.indexOf("pone ") >= 0) && visualNounCount >= 1) {
        return true;
    }

    return false;
}

void CopitoTools::generateFallbackSketch(const String &query, String &outSketch) {
    String q = query;
    q.toLowerCase();
    q.replace("á", "a"); q.replace("é", "e"); q.replace("í", "i");
    q.replace("ó", "o"); q.replace("ú", "u"); q.replace("ñ", "n");
    const char *presetName = "ROBOT";

    // Detectores semánticos para composición modular (Scene Composer)
    bool hasHouse = (q.indexOf("casa") >= 0 || q.indexOf("casita") >= 0 || q.indexOf("hogar") >= 0 || q.indexOf("house") >= 0);
    bool hasSun   = (q.indexOf("sol") >= 0 || q.indexOf("solcito") >= 0 || q.indexOf("sun") >= 0 || q.indexOf("dia") >= 0);
    bool hasCow   = (q.indexOf("vaca") >= 0 || q.indexOf("vaquita") >= 0 || q.indexOf("cow") >= 0);
    bool hasMoon  = (q.indexOf("luna") >= 0 || q.indexOf("noche") >= 0 || q.indexOf("moon") >= 0);
    bool hasSmile = (q.indexOf("sonri") >= 0 || q.indexOf("dient") >= 0 || q.indexOf("feliz") >= 0 ||
                     q.indexOf("alegre") >= 0 || q.indexOf("cara") >= 0 || q.indexOf("boca") >= 0 ||
                     q.indexOf("sonrisa") >= 0);
    bool hasTree  = (q.indexOf("arbol") >= 0 || q.indexOf("arbolito") >= 0 || q.indexOf("planta") >= 0 || q.indexOf("tree") >= 0);

    // --- ESCENAS MODULARES COMPUESTAS (SCENE COMPOSER) ---
    if (hasHouse && (hasCow || hasSun)) {
        if (hasCow) {
            // Escena completa: Casita campestre + Sol radiante + Suelo + Vaca con manchas
            presetName = "ESCENA_CASA_SOL_VACA";
            outSketch = "CLEAR; MACRO SUN 165 35 15; MACRO HOUSE 25 105 60 65; LINE 0 170 200 170 2; MACRO COW 115 125";
        } else {
            // Escena campo: Casita + Sol + Arbol + Nube + Suelo
            presetName = "ESCENA_CASA_CAMPO";
            outSketch = "CLEAR; MACRO SUN 165 35 15; MACRO HOUSE 30 100 70 70; LINE 0 170 200 170 2; MACRO TREE 155 140; MACRO CLOUD 60 35";
        }
    } else if (hasMoon && hasSmile) {
        // Luna sonriente con curva Bézier, ojos, pestañas, sonrisa y cielo estrellado
        presetName = "LUNA_SONRIENTE";
        outSketch = "CLEAR; MACRO MOON_SMILE 90 95; MACRO STAR 30 40 6; MACRO STAR 165 45 7; MACRO STAR 40 140 5; MACRO STAR 160 145 6; MACRO STAR 130 175 4";
    } else if (hasCow) {
        // Vaca en pradera con árbol, sol y nube
        presetName = "VACA";
        outSketch = "CLEAR; MACRO SUN 165 35 15; MACRO COW 65 115; LINE 0 160 200 160 2; MACRO TREE 30 130; MACRO CLOUD 55 35";
    } else if (q.indexOf("nino") >= 0 || q.indexOf("nina") >= 0 || q.indexOf("chico") >= 0 ||
               q.indexOf("chica") >= 0 || q.indexOf("pibe") >= 0 || q.indexOf("persona") >= 0 ||
               q.indexOf("hombre") >= 0 || q.indexOf("nene") >= 0 || q.indexOf("nena") >= 0 ||
               q.indexOf("jugando") >= 0 || q.indexOf("juego") >= 0) {
        presetName = "NINO_JUGANDO";
        outSketch = "CLEAR; MACRO SUN 155 35 15; MACRO CLOUD 45 30; LINE 0 165 200 165 2; CIRCLE 100 75 14 0; LINE 100 89 100 135 3; LINE 100 100 65 115 3; LINE 65 115 50 140 3; LINE 100 100 135 115 3; LINE 135 115 150 95 3; LINE 100 135 75 165 3; LINE 100 135 125 165 3; FCIRCLE 45 150 10 0; CIRCLE 45 150 10 1";
    } else if (q.indexOf("dragon") >= 0 || q.indexOf("drag") >= 0 || q.indexOf("dinosaurio") >= 0 ||
               q.indexOf("dino") >= 0 || q.indexOf("monstruo") >= 0) {
        presetName = "DRAGON";
        outSketch = "CLEAR; LINE 0 175 200 175 2; LINE 30 175 45 160 2; LINE 45 160 60 175 2; LINE 150 175 165 165 2; LINE 165 165 180 175 2; LINE 70 55 45 62 3; LINE 45 62 48 70 3; LINE 48 70 65 68 2; LINE 65 68 52 78 2; LINE 52 78 72 76 2; LINE 52 65 55 68 2; LINE 58 66 61 68 2; FCIRCLE 64 57 2 0; LINE 70 55 88 38 3; LINE 74 57 92 44 2; CURVE 72 76 85 100 95 115 3; CURVE 74 57 95 75 105 95 3; LINE 80 64 88 58 2; LINE 90 75 99 68 2; LINE 98 87 108 80 2; LINE 106 99 117 92 2; LINE 116 110 127 103 2; LINE 126 120 137 114 2; CURVE 95 115 105 142 130 135 3; CURVE 105 95 125 105 135 125 2; LINE 102 95 135 30 3; LINE 135 30 180 40 3; LINE 135 30 170 65 2; LINE 135 30 155 80 2; CURVE 180 40 175 55 170 65 2; CURVE 170 65 162 75 155 80 2; CURVE 155 80 135 95 118 105 2; FCIRCLE 135 30 2 0; LINE 98 128 88 155 3; LINE 88 155 78 162 2; LINE 88 155 86 165 2; LINE 88 155 96 163 2; LINE 128 132 132 158 3; LINE 132 158 122 165 2; LINE 132 158 130 168 2; LINE 132 158 140 165 2; CURVE 130 135 160 145 165 165 3; CURVE 165 165 168 180 185 170 3; LINE 185 170 198 160 2; LINE 185 170 195 178 2; LINE 198 160 195 178 2; CURVE 46 74 25 70 8 55 2; CURVE 48 76 28 82 12 78 2; CURVE 50 77 30 95 14 98 2; FCIRCLE 21 75 3 0; FCIRCLE 10 64 3 0";
    } else if (q.indexOf("gato") >= 0 || q.indexOf("michi") >= 0 || q.indexOf("gatito") >= 0 || q.indexOf("cat") >= 0) {
        presetName = "GATO";
        outSketch = "CLEAR; CIRCLE 100 110 50 0; LINE 60 75 45 35 2; LINE 45 35 85 65 2; LINE 140 75 155 35 2; LINE 155 35 115 65 2; FCIRCLE 80 100 7 0; FCIRCLE 120 100 7 0; FCIRCLE 100 120 4 0; LINE 100 124 100 132 2; LINE 100 132 90 138 2; LINE 100 132 110 138 2; LINE 50 115 20 110 1; LINE 50 125 20 130 1; LINE 150 115 180 110 1; LINE 150 125 180 130 1";
    } else if (hasHouse) {
        presetName = "CASA";
        outSketch = "CLEAR; RECT 50 90 100 80 0; LINE 40 90 100 40 3; LINE 100 40 160 90 3; RECT 85 125 30 45 0; RECT 120 105 20 20 0; LINE 130 105 130 125 1; LINE 120 115 140 115 1; RECT 135 45 15 25 0; LINE 60 170 140 170 2";
    } else if (q.indexOf("cohete") >= 0 || q.indexOf("nave") >= 0 || q.indexOf("espacio") >= 0 || q.indexOf("rocket") >= 0) {
        presetName = "COHETE";
        outSketch = "CLEAR; LINE 100 25 75 80 2; LINE 100 25 125 80 2; LINE 75 80 75 140 2; LINE 125 80 125 140 2; LINE 75 140 125 140 2; CIRCLE 100 95 14 0; FCIRCLE 100 95 4 0; LINE 75 120 50 155 2; LINE 50 155 75 140 2; LINE 125 120 150 155 2; LINE 150 155 125 140 2; LINE 85 140 90 170 2; LINE 100 140 100 180 2; LINE 115 140 110 170 2";
    } else if (q.indexOf("auto") >= 0 || q.indexOf("coche") >= 0 || q.indexOf("carro") >= 0 || q.indexOf("car") >= 0 || q.indexOf("autito") >= 0) {
        presetName = "AUTO";
        outSketch = "CLEAR; RECT 40 110 120 35 0; LINE 60 110 80 80 2; LINE 80 80 130 80 2; LINE 130 80 145 110 2; FCIRCLE 65 145 12 0; CIRCLE 65 145 6 1; FCIRCLE 135 145 12 0; CIRCLE 135 145 6 1; LINE 40 145 53 145 2; LINE 77 145 123 145 2; LINE 147 145 160 145 2; RECT 83 85 20 20 0; RECT 108 85 20 20 0";
    } else if (q.indexOf("kraken") >= 0 || q.indexOf("pulpo") >= 0 || q.indexOf("calamar") >= 0 || q.indexOf("medusa") >= 0 || q.indexOf("mar") >= 0) {
        presetName = "KRAKEN";
        outSketch = "CLEAR; CIRCLE 100 75 35 0; FCIRCLE 85 70 6 0; FCIRCLE 115 70 6 0; LINE 65 95 45 130 3; LINE 45 130 35 170 3; LINE 80 105 70 145 3; LINE 70 145 55 180 3; LINE 95 110 90 155 3; LINE 90 155 85 190 3; LINE 105 110 110 155 3; LINE 110 155 115 190 3; LINE 120 105 130 145 3; LINE 130 145 145 180 3; LINE 135 95 155 130 3; LINE 155 130 165 170 3; LINE 20 190 60 185 2; LINE 60 185 100 190 2; LINE 100 190 140 185 2; LINE 140 185 180 190 2";
    } else if (q.indexOf("edificio") >= 0 || q.indexOf("iglesia") >= 0 || q.indexOf("cruz") >= 0 || q.indexOf("torre") >= 0 || q.indexOf("ciudad") >= 0 || q.indexOf("castillo") >= 0) {
        presetName = "EDIFICIO";
        outSketch = "CLEAR; RECT 50 60 100 135 0; LINE 50 60 100 25 3; LINE 100 25 150 60 3; LINE 100 10 100 25 3; LINE 90 16 110 16 3; CIRCLE 100 50 12 0; FCIRCLE 100 50 3 0; RECT 85 145 30 50 0; LINE 85 145 100 135 2; LINE 100 135 115 145 2; RECT 65 80 20 25 0; RECT 115 80 20 25 0; RECT 65 115 20 25 0; RECT 115 115 20 25 0";
    } else if (q.indexOf("barco") >= 0 || q.indexOf("velero") >= 0 || q.indexOf("bote") >= 0 || q.indexOf("yate") >= 0) {
        presetName = "BARCO";
        outSketch = "CLEAR; LINE 30 145 170 145 3; LINE 30 145 50 175 3; LINE 50 175 150 175 3; LINE 150 175 170 145 3; LINE 100 35 100 145 3; LINE 100 40 150 135 2; LINE 150 135 100 135 2; LINE 100 55 60 135 2; LINE 60 135 100 135 2; LINE 15 185 45 180 2; LINE 45 180 75 185 2; LINE 75 185 105 180 2; LINE 105 180 135 185 2; LINE 135 185 165 180 2; LINE 165 180 185 185 2";
    } else if (q.indexOf("avion") >= 0 || q.indexOf("helicoptero") >= 0 || q.indexOf("vuelo") >= 0 || q.indexOf("alas") >= 0) {
        presetName = "AVION";
        outSketch = "CLEAR; LINE 20 100 180 100 3; LINE 180 100 160 85 2; LINE 160 85 140 100 2; LINE 80 100 60 40 3; LINE 60 40 90 100 2; LINE 80 100 60 160 3; LINE 60 160 90 100 2; LINE 30 100 20 70 2; LINE 20 70 40 100 2";
    } else if (q.indexOf("montana") >= 0 || q.indexOf("paisaje") >= 0 || q.indexOf("volcan") >= 0 || q.indexOf("cerro") >= 0) {
        presetName = "MONTANA";
        outSketch = "CLEAR; LINE 20 160 80 80 3; LINE 80 80 130 145 3; LINE 110 120 150 70 3; LINE 150 70 185 160 3; LINE 10 160 190 160 3; CIRCLE 60 55 18 0; FCIRCLE 60 55 12 0; LINE 60 30 60 22 2; LINE 35 55 27 55 2; LINE 85 55 93 55 2; LINE 42 37 36 31 2; LINE 78 37 84 31 2; LINE 80 80 75 100 1; LINE 80 80 87 100 1; LINE 150 70 145 90 1; LINE 150 70 157 90 1";
    } else if (q.indexOf("espada") >= 0 || q.indexOf("escudo") >= 0 || q.indexOf("armadura") >= 0 || q.indexOf("caballero") >= 0) {
        presetName = "ESCUDO";
        outSketch = "CLEAR; LINE 50 40 150 40 3; LINE 50 40 50 110 3; LINE 150 40 150 110 3; LINE 50 110 100 170 3; LINE 150 110 100 170 3; LINE 60 50 140 50 1; LINE 60 50 60 105 1; LINE 140 50 140 105 1; LINE 60 105 100 158 1; LINE 140 105 100 158 1; FCIRCLE 100 95 20 0; LINE 100 65 100 125 3; LINE 70 95 130 95 3";
    } else if (q.indexOf("flor") >= 0 || q.indexOf("flower") >= 0 || q.indexOf("florcita") >= 0) {
        presetName = "FLOR";
        outSketch = "CLEAR; FCIRCLE 100 70 14 0; FCIRCLE 100 110 14 0; FCIRCLE 80 90 14 0; FCIRCLE 120 90 14 0; FCIRCLE 100 90 16 1; CIRCLE 100 90 16 0; LINE 100 124 100 185 3; LINE 100 150 125 135 2; LINE 100 160 75 145 2";
    } else if (hasSun) {
        presetName = "SOL";
        outSketch = "CLEAR; CIRCLE 100 100 35 0; FCIRCLE 100 100 30 0; LINE 100 40 100 55 3; LINE 100 145 100 160 3; LINE 40 100 55 100 3; LINE 145 100 160 100 3; LINE 58 58 68 68 3; LINE 132 132 142 142 3; LINE 142 58 132 68 3; LINE 58 142 68 132 3";
    } else if (q.indexOf("corazon") >= 0 || q.indexOf("amor") >= 0 || q.indexOf("heart") >= 0) {
        presetName = "CORAZON";
        outSketch = "CLEAR; FCIRCLE 80 80 28 0; FCIRCLE 120 80 28 0; LINE 53 90 100 155 3; LINE 147 90 100 155 3; FCIRCLE 100 95 38 0";
    } else if (q.indexOf("estrella") >= 0 || q.indexOf("star") >= 0) {
        presetName = "ESTRELLA";
        outSketch = "CLEAR; LINE 100 30 118 75 2; LINE 118 75 168 75 2; LINE 168 75 128 105 2; LINE 128 105 145 155 2; LINE 145 155 100 125 2; LINE 100 125 55 155 2; LINE 55 155 72 105 2; LINE 72 105 32 75 2; LINE 32 75 82 75 2; LINE 82 75 100 30 2";
    } else if (q.indexOf("perro") >= 0 || q.indexOf("perrito") >= 0 || q.indexOf("can") >= 0 || q.indexOf("dog") >= 0) {
        presetName = "PERRO";
        outSketch = "CLEAR; CIRCLE 100 105 45 0; FCIRCLE 75 95 6 0; FCIRCLE 125 95 6 0; FCIRCLE 100 115 8 0; LINE 100 123 100 133 2; LINE 100 133 88 138 2; LINE 100 133 112 138 2; LINE 65 70 45 105 4; LINE 135 70 155 105 4; LINE 50 115 20 115 1; LINE 150 115 180 115 1";
    } else if (q.indexOf("cafe") >= 0 || q.indexOf("taza") >= 0 || q.indexOf("mate") >= 0 || q.indexOf("coffee") >= 0) {
        presetName = "CAFE";
        outSketch = "CLEAR; RECT 60 90 75 60 0; CIRCLE 135 115 18 0; LINE 50 152 145 152 3; LINE 75 75 75 60 2; LINE 85 78 85 55 2; LINE 100 75 100 58 2; LINE 115 78 115 62 2";
    } else if (hasMoon) {
        presetName = "LUNA";
        outSketch = "CLEAR; CIRCLE 95 100 50 0; FCIRCLE 115 90 45 1; MACRO STAR 40 45 6; MACRO STAR 165 45 5; MACRO STAR 40 150 5; MACRO STAR 160 150 6";
    } else if (hasTree) {
        presetName = "ARBOL";
        outSketch = "CLEAR; RECT 90 120 20 60 0; CIRCLE 100 80 42 0; FCIRCLE 80 85 24 0; FCIRCLE 120 85 24 0; FCIRCLE 100 60 28 0; LINE 60 180 140 180 2";
    } else if (q.indexOf("libro") >= 0 || q.indexOf("cuaderno") >= 0 || q.indexOf("carta") >= 0 || q.indexOf("lectura") >= 0) {
        presetName = "LIBRO";
        outSketch = "CLEAR; RECT 40 60 120 80 0; LINE 100 60 100 140 3; LINE 40 60 100 70 2; LINE 160 60 100 70 2; LINE 40 140 100 150 2; LINE 160 140 100 150 2; LINE 55 85 85 85 1; LINE 55 105 85 105 1; LINE 115 85 145 85 1; LINE 115 105 145 105 1";
    } else {
        uint32_t h = 0;
        for (size_t i = 0; i < q.length(); i++) {
            h = (h * 31) + (uint8_t)q.charAt(i);
        }
        int variant = h % 4;
        if (variant == 0) {
            presetName = "ROBOT";
            outSketch = "CLEAR; RECT 50 60 100 80 0; RECT 60 70 80 45 0; FCIRCLE 80 90 8 0; FCIRCLE 120 90 8 0; LINE 85 125 115 125 2; LINE 100 60 100 35 2; FCIRCLE 100 32 6 0; LINE 50 95 30 115 2; LINE 150 95 170 115 2; LINE 75 140 75 175 3; LINE 125 140 125 175 3";
        } else if (variant == 1) {
            presetName = "MANDALA";
            outSketch = "CLEAR; CIRCLE 100 100 60 0; CIRCLE 100 100 45 0; CIRCLE 100 100 30 0; FCIRCLE 100 100 12 0; LINE 100 25 100 175 2; LINE 25 100 175 100 2; LINE 47 47 153 153 2; LINE 47 153 153 47 2; FCIRCLE 100 40 5 0; FCIRCLE 100 160 5 0; FCIRCLE 40 100 5 0; FCIRCLE 160 100 5 0";
        } else if (variant == 2) {
            presetName = "MONTANA";
            outSketch = "CLEAR; LINE 20 160 80 80 3; LINE 80 80 130 145 3; LINE 110 120 150 70 3; LINE 150 70 185 160 3; LINE 10 160 190 160 3; CIRCLE 60 55 18 0; FCIRCLE 60 55 12 0; LINE 60 30 60 22 2; LINE 35 55 27 55 2; LINE 85 55 93 55 2; LINE 42 37 36 31 2; LINE 78 37 84 31 2";
        } else {
            presetName = "ESCUDO";
            outSketch = "CLEAR; LINE 50 40 150 40 3; LINE 50 40 50 110 3; LINE 150 40 150 110 3; LINE 50 110 100 170 3; LINE 150 110 100 170 3; LINE 60 50 140 50 1; LINE 60 50 60 105 1; LINE 140 50 140 105 1; LINE 60 105 100 158 1; LINE 140 105 100 158 1; FCIRCLE 100 95 20 0; LINE 100 65 100 125 3; LINE 70 95 130 95 3";
        }
    }
    CLOGI("TOOLS", "Boceto fallback: preset '%s' para query '%s'", presetName, q.c_str());
}

// Respuestas 100% locales para la ruta de texto (portal /api/chat).
// Ahorra una llamada a Gemini cuando preguntan sensores/bateria/hora.
bool CopitoTools::answerLocalText(const String &msg, String &outReply, int &outDraw) {
    String m = msg;
    m.toLowerCase();

    bool isTemp = (m.indexOf("temperatur") >= 0 || m.indexOf("calor") >= 0 || m.indexOf("fri") >= 0 || m.indexOf("clima") >= 0);
    bool isHum  = (m.indexOf("humedad") >= 0);
    bool isBat  = (m.indexOf("bateria") >= 0 || m.indexOf("bater") >= 0 || m.indexOf("pila") >= 0 || m.indexOf("carga") >= 0);
    bool isHora = (m.indexOf("hora") >= 0 || m.indexOf("reloj") >= 0);
    if (!isTemp && !isHum && !isBat && !isHora) return false;

    String probe = "[TOOL:GET_TEMP]|[TOOL:GET_HUM]|[TOOL:GET_BATERIA]|[TOOL:GET_HORA]";
    resolveToolTags(probe);
    int p1 = probe.indexOf('|');
    int p2 = probe.indexOf('|', p1 + 1);
    int p3 = probe.indexOf('|', p2 + 1);
    String vTemp = probe.substring(0, p1);
    String vHum  = probe.substring(p1 + 1, p2);
    String vBat  = probe.substring(p2 + 1, p3);
    String vHora = probe.substring(p3 + 1);

    if (isTemp && !isHum && !isBat && !isHora) {
        outReply = "Ahora mismo hay " + vTemp + " en mi cuarto.";
        outDraw = 4; // NUBE (clima neutro)
        return true;
    }
    if (isHum && !isBat) {
        outReply = "La humedad ahora es de " + vHum + ".";
        outDraw = 4;
        return true;
    }
    if (isBat) {
        outReply = "Mi bateria esta en " + vBat + ".";
        outDraw = 10; // RAYO (energia)
        return true;
    }
    if (isHora) {
        outReply = "Ahora " + vHora + ".";
        outDraw = 0;
        return true;
    }
    return false;
}

void CopitoTools::syncNtp() {
    configTime(-3 * 3600, 0, "time.google.com", "pool.ntp.org", "time.cloudflare.com");
    struct tm tm;
    if (getLocalTime(&tm, 3000)) {
        if (tm.tm_year >= 124) { // Año >= 2024
            g_ntpSynced = true;
            log_i("NTP sincronizado con exito: %02d:%02d:%02d (%02d/%02d/%04d)",
                  tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
            rtcDriver.syncRtcFromSystemTime();
            return;
        }
    }
    log_w("NTP aun no disponible. Reintento en curso.");
}

bool CopitoTools::buildClockCard(String &outCard) {
    struct tm tm;
    if (!getLocalTime(&tm, 500)) return false;
    char clock[8];
    snprintf(clock, sizeof(clock), "%02d:%02d", tm.tm_hour, tm.tm_min);
    outCard = String(clock);
    if (g_ambientSensorValid) {
        char env[32];
        snprintf(env, sizeof(env), "\n%dC %d%%", (int)round(g_ambientTempC), (int)round(g_ambientHum));
        outCard += env;
    }
    return true;
}
