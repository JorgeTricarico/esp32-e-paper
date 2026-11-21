# 💬 SESIÓN 09 — MEMORIA DE CONVERSACIÓN Y CONTEXTO PERSISTENTE

**Dominio:** Inteligencia Artificial — Contexto y Personalización Profunda  
**Prioridad:** 🟡 Media — Transforma Copito de "asistente puntual" a "compañero real"  
**Prerequisitos:** Sesión 04 (Function Calling) recomendada pero no obligatoria

---

## 🎯 Objetivo de la Sesión

Darle a Copito **memoria de conversación** para que cada interacción tenga
contexto de las anteriores, y **memoria a largo plazo** para que recuerde
datos importantes sobre Jorge y sus preferencias.

---

## 📦 Mejoras Planificadas

### A) Historial de conversación en PSRAM (corto plazo)

Guardar las últimas N interacciones en un buffer circular en PSRAM y
enviarlas como contexto en cada llamada a Gemini.

**Estructura:**
```cpp
struct ConversationTurn {
    char userText[256];    // Lo que dijo el usuario (transcripto por Gemini)
    char assistantText[512]; // Lo que respondió Copito
    uint32_t timestamp;
};
static ConversationTurn history[MAX_HISTORY_TURNS]; // En PSRAM
static int historyHead = 0;
static int historyCount = 0;
```

**Impacto en el prompt Gemini:**  
Agregar el historial como "conversación previa" antes del audio del usuario.
Latencia adicional: ~100-200ms por el mayor tamaño del JSON.

**Configurable desde portal:**
- `max_history_turns`: 0 (sin memoria) a 10 turnos

---

### B) Memoria a largo plazo en NVS

Hechos importantes sobre Jorge que Copito recuerda entre reinicios:
- Nombre, preferencias, intereses
- Última vez que se habló de un tema
- Notas que Jorge le pidió guardar

**Implementación:**
- Endpoint `POST /remember` en el portal web
- Copito puede actualizar la memoria via function calling de Gemini
  cuando detecta frases como "recordá que me gusta..." o "anotá que..."

---

### C) Transcripción del audio a texto para historial

Actualmente Gemini procesa el audio directamente (multimodal).
Para el historial, necesitamos texto. Opciones:
1. Pedirle a Gemini que devuelva también la transcripción del audio en el JSON
2. Usar un endpoint dedicado de STT antes de la respuesta

---

### D) Modos de personalidad seleccionables

Con 2 clicks del botón (Sesión 07), cambiar entre modos:
- **Normal:** Conversacional y cálido
- **Conciso:** Respuestas de máximo 2 oraciones
- **Técnico:** Respuestas detalladas con datos y fuentes
- **Creativo:** Alta temperatura, más imaginativo

Cada modo tiene su propio system prompt guardado en config.

---

## ⚠️ Consideraciones Técnicas

- El historial aumenta el tamaño del JSON enviado a Gemini → más latencia
- En PSRAM (8MB disponibles): buffer de 10 turnos ≈ ~8KB, muy manejable
- La transcripción agrega latencia pero habilita búsqueda y análisis del historial

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN

```
Retomamos el proyecto Copito AI (ESP32-S3-ePaper-1.54).
Lee primero el workflow en docs/WORKFLOW_AGENTE.md.
Hoy implementaremos la Sesión 09: Memoria de conversación.
Quiero que Copito recuerde las últimas 5 interacciones y las incluya como contexto
en las llamadas a Gemini. Usar un buffer circular en PSRAM.
El punto de entrada es queryGeminiAudio() en src/ai_engine.cpp.
```
