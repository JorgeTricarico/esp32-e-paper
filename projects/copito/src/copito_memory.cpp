#include "copito_memory.h"
#include <Preferences.h>

CopitoMemory copitoMem;

void CopitoMemory::begin() {
    memset(turns, 0, sizeof(turns));
    head = 0;
    count = 0;
    factsLoaded = false;
    loadFacts();
}

void CopitoMemory::addTurn(const String &userText, const String &assistantText) {
    CopitoTurn &t = turns[head];
    strncpy(t.user, userText.c_str(), sizeof(t.user) - 1);
    t.user[sizeof(t.user) - 1] = 0;
    strncpy(t.assistant, assistantText.c_str(), sizeof(t.assistant) - 1);
    t.assistant[sizeof(t.assistant) - 1] = 0;
    head = (head + 1) % COPITO_MAX_TURNS;
    if (count < COPITO_MAX_TURNS) count++;
}

String CopitoMemory::buildContextBlock() {
    String out = "";
    // 1. Datos largos
    loadFacts();
    if (factsCache.length() > 0) {
        out += "\nDATOS QUE RECUERDAS DE JORGE: " + factsCache + "\n";
    }
    // 2. Charla reciente (orden viejo -> nuevo)
    if (count > 0) {
        out += "ULTIMAS CONVERSACIONES:\n";
        int start = (count < COPITO_MAX_TURNS) ? 0 : head;
        for (int i = 0; i < count; i++) {
            int idx = (start + i) % COPITO_MAX_TURNS;
            if (strlen(turns[idx].user) == 0 && strlen(turns[idx].assistant) == 0) continue;
            out += "- Jorge: ";
            out += turns[idx].user;
            out += " | Copito: ";
            out += turns[idx].assistant;
            out += "\n";
        }
    }
    // Limitar tamanio para no agrandar JSON (max ~1200 chars)
    if (out.length() > 1200) {
        out = out.substring(out.length() - 1200);
    }
    return out;
}

void CopitoMemory::loadFacts() {
    if (factsLoaded) return;
    Preferences p;
    if (p.begin("copito_mem", true)) {
        factsCache = p.getString("facts", "");
        p.end();
    }
    factsLoaded = true;
}

String CopitoMemory::getFacts() {
    loadFacts();
    return factsCache;
}

bool CopitoMemory::saveFact(const String &fact) {
    loadFacts();
    String f = fact;
    f.trim();
    if (f.length() == 0) return false;
    // Evitar duplicados exactos
    if (factsCache.indexOf(f) >= 0) return true;
    String next = factsCache;
    if (next.length() > 0) next += " | ";
    next += f;
    // Tope ~900 chars (NVS tranquilo). Si se pasa, recorta lo mas viejo.
    while (next.length() > 900) {
        int sep = next.indexOf(" | ");
        if (sep < 0) break;
        next = next.substring(sep + 3);
    }
    Preferences p;
    if (!p.begin("copito_mem", false)) return false;
    p.putString("facts", next);
    p.end();
    factsCache = next;
    return true;
}

void CopitoMemory::clearAll() {
    Preferences p;
    if (p.begin("copito_mem", false)) {
        p.clear();
        p.end();
    }
    factsCache = "";
    memset(turns, 0, sizeof(turns));
    head = 0;
    count = 0;
}
