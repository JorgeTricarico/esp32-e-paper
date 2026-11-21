# ⚡ Sesión 1: Rendimiento, Latencia y Optimización de Memoria

> **Objetivo de la sesión:** Reducir la latencia total de respuesta de voz a menos de 1.8 segundos y optimizar la gestión de memoria en PSRAM de 8MB.

---

## 🎯 Metas Específicas
1. **Reducción de Latencia End-to-End:**
   - Reducir el tiempo entre que el usuario termina de hablar y suena la primera palabra a < 1.8s.
   - Implementar HTTP Keep-Alive / Socket Reuse para la conexión con Gemini y Google TTS (evitar el handshake TLS de ~300ms en cada turno).
2. **Distribución Multi-Core en FreeRTOS:**
   - Core 0: Refresco asíncrono de pantalla e-Paper (evita bloquear el audio por ~300ms).
   - Core 1: Captura de micrófono, inferencia HTTPS de Gemini y decodificación minimp3.
3. **Manejo de Memoria en PSRAM OPI (8MB):**
   - Evitar cualquier asignación mayor a 4KB en el heap interno de SRAM.
   - Usar ring-buffers en PSRAM para streaming de audio continuo sin microcortes.

---

## 🛠️ Archivos Clave a Modificar
- [`src/ai_engine.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/ai_engine.cpp) (reutilización de sockets HTTPS y streaming)
- [`src/main.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/main.cpp) (ajustes de afinidad de núcleos FreeRTOS y prioridades)
- [`src/epaper_driver.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/epaper_driver.cpp) (cola de renderizado en background)

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN (Copiar y Pegar)

```text
Hola, hoy vamos a trabajar en la Sesión 1 de Optimización: Rendimiento, Latencia y Memoria para Copito en ESP32-S3.
Por favor lee primero docs/WORKFLOW_AGENTE.md y docs/roadmap/01_PERFORMANCE_Y_LATENCIA.md.

Quiero que nos enfoquemos en:
1. Medir con precisión los tiempos de cada fase de la interacción (tiempo de VAD, tiempo de upload a Gemini, tiempo de generación de respuesta, tiempo de conexión TTS y primer audio).
2. Implementar socket reuse / keep-alive o pre-conexión TLS para eliminar el retardo de handshake SSL.
3. Asegurar que las tareas pesadas de red y decodificación MP3 utilicen la PSRAM sin fragmentación.
4. Compilar, verificar el consumo de RAM/Flash y flashear para comprobar la mejora en vivo.
```
