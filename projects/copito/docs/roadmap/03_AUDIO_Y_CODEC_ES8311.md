# 🔊 Sesión 3: Calidad Acústica, Codec ES8311 y Procesamiento de Señal

> **Objetivo de la sesión:** Elevar la fidelidad de audio al máximo nivel posible en el hardware: ecualización de parlante, reducción de ruido en micrófono, soundscapes robóticos armónicos y opciones avanzadas de TTS.

---

## 🎯 Metas Específicas
1. **Ecualización y Filtros para Parlante de 3W (NS4150B):**
   - El pequeño altavoz de cavidad acústica resuena entre 300Hz y 1.2kHz y pierde graves profundos.
   - Configurar filtros digitales (high-pass a 150Hz para evitar distorsión de excursión y suave boost en 2.5kHz para claridad vocal).
   - Implementar control dinámico de rango (soft limiter / compressor) para evitar saturación ante picos repentinos.
2. **Optimización del Micrófono y Supresión de Ruido:**
   - Calibrar la compuerta de ruido (noise gate) para descartar ventiladores o ruido de fondo en el cuarto de Jorge.
   - Algoritmo de filtrado DC offset y filtro paso alto en el stream de captura PCM antes de empaquetar el WAV para Gemini.
3. **Paisajes Sonoros y Notificaciones Acústicas:**
   - Expandir la biblioteca de chimes armónicos suaves (sonido de "escuchando", "pensando", "éxito", "alerta leve").
   - Transiciones suaves entre notas con decaimiento exponencial (envolvente ADSR realista).

---

## 🛠️ Archivos Clave a Modificar
- [`src/audio_es8311.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/audio_es8311.cpp) (filtros, registrador ES8311, síntesis senoidal)
- [`include/audio_es8311.h`](file:///C:/Users/admin/Documents/Github/esp32-paper/include/audio_es8311.h) (definiciones acústicas y tipos de chimes)

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN (Copiar y Pegar)

```text
Hola, hoy vamos a trabajar en la Sesión 3 de Audio, Acústica y Codec ES8311 para Copito en ESP32-S3.
Por favor lee primero docs/WORKFLOW_AGENTE.md y docs/roadmap/03_AUDIO_Y_CODEC_ES8311.md.

Quiero que nos enfoquemos en:
1. Ajustar los filtros de audio del parlante para que la voz humana suene cálida, presente y sin ninguna resonancia molesta.
2. Afinar la sensibilidad del micrófono analógico y la compuerta de ruido para que capture mi voz perfectamente a 1-2 metros.
3. Diseñar nuevos sonidos robóticos agradables y sutiles tipo compañero retro-futurista (chimes armónicos con envolvente suave).
4. Compilar y flashear para hacer pruebas de audio y captura en vivo con el parlante de la placa.
```
