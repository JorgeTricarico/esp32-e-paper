#ifndef COPITO_MEMORY_H
#define COPITO_MEMORY_H

#include <Arduino.h>

// Memoria corta: ultimos 5 turnos en PSRAM/RAM. Larga: datos en NVS.
#define COPITO_MAX_TURNS 5

struct CopitoTurn {
    char user[160];
    char assistant[256];
};

class CopitoMemory {
public:
    void begin();
    // userText: lo que dijo Jorge (transcripcion corta o "(voz)"). assistant: respuesta ya limpia
    void addTurn(const String &userText, const String &assistantText);
    // Arma bloquecito de contexto para inyectar en el prompt de Gemini
    String buildContextBlock();
    // Memoria larga
    bool saveFact(const String &fact);
    String getFacts();
    void clearAll();

private:
    CopitoTurn turns[COPITO_MAX_TURNS];
    int head = 0;
    int count = 0;
    String factsCache = "";
    bool factsLoaded = false;
    void loadFacts();
};

extern CopitoMemory copitoMem;

#endif
