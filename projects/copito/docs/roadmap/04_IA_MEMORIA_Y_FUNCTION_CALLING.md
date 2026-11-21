# 🧠 Sesión 4: Inteligencia Artificial, Memoria Conversacional y Function Calling

> **Objetivo de la sesión:** Dotar a Copito de memoria a largo plazo (para recordar gustos, proyectos y charlas previas con Jorge) e integrar Function Calling con los sensores y actuadores físicos de la placa.

---

## 🎯 Metas Específicas
1. **Memoria Persistente de Compañero (Long-Term Memory):**
   - Almacenar en la memoria Flash/NVS un diario compacto o resumen de hechos sobre Jorge (ej: proyectos en curso, música favorita, preferencias de estilo).
   - Inyectar el resumen de contexto en el system prompt de Gemini para que las respuestas sean coherentes a lo largo de días o semanas.
2. **Llamadas a Funciones de Hardware (Function Calling):**
   - Permitir que Gemini ejecute acciones directas en la placa según lo que pida el usuario:
     - `getRoomSensorData()`: Consulta temperatura y humedad del sensor SHTC3 en tiempo real.
     - `setAlarm(time)`: Configura una alarma física en el chip RTC PCF85063.
     - `changeFace(emotion)`: Cambia la cara de la pantalla e-Ink a una emoción específica.
     - `drawCanvas(drawingName)`: Dibuja figuras personalizadas en la pantalla.
3. **Optimización de Prompting y Respuestas:**
   - Afinar el prompt para que distinga entre preguntas que requieren datos locales vs consultas de razonamiento general.

---

## 🛠️ Archivos Clave a Modificar
- [`src/ai_engine.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/ai_engine.cpp) (soporte para JSON function calling de Gemini y guardado de memoria)
- [`include/config.h`](file:///C:/Users/admin/Documents/Github/esp32-paper/include/config.h) (definición de herramientas y esquema de memoria)
- [`src/main.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/main.cpp) (ejecución de callbacks de funciones de hardware)

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN (Copiar y Pegar)

```text
Hola, hoy vamos a trabajar en la Sesión 4 de IA, Memoria y Function Calling para Copito en ESP32-S3.
Por favor lee primero docs/WORKFLOW_AGENTE.md y docs/roadmap/04_IA_MEMORIA_Y_FUNCTION_CALLING.md.

Quiero que nos enfoquemos en:
1. Implementar un sistema de memoria para que Copito recuerde información clave sobre mí (Jorge) entre reinicios.
2. Añadir Function Calling para que Copito pueda leer los sensores físicos (temperatura/humedad SHTC3, batería, hora RTC) cuando se lo pregunte por voz.
3. Permitir que Copito dibuje cosas específicas en la pantalla de tinta electrónica cuando el tema lo amerite.
4. Compilar, flashear y realizar una prueba conversacional en vivo para verificar la memoria y las funciones.
```
