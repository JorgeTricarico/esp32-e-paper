# 🌐 Sesión 6: Web Dashboard Avanzado, Telemetría y Conectividad

> **Objetivo de la sesión:** Convertir el portal web en una consola interactiva completa accesible desde el navegador del celular o PC en `http://copito.local` o por IP local.

---

## 🎯 Metas Específicas
1. **Resolución mDNS Local:**
   - Habilitar `ESPmDNS` para acceder escribiendo directamente `http://copito.local` en cualquier navegador de la red local sin necesidad de recordar la dirección IP.
2. **Telemetría y Monitor en Vivo:**
   - Endpoint de streaming o WebSocket para visualizar en la web:
     - El contenido exacto de la pantalla e-Paper reflejado en un canvas HTML en tiempo real.
     - Nivel de señal WiFi, memoria libre, temperatura ambiente y estado de batería en tiempo real.
     - Historial de las últimas interacciones y respuestas de texto.
3. **Logs persistentes + consola web (IMPORTANTE — incidente audio 2026-09-06):**
   - Un bug intermitente (ej: audio saturado una vez cada N usos) es imposible de diagnosticar sin USB. El dispositivo debe guardar sus propios logs y exponerlos en la web.
   - Diseño: ring buffer circular en PSRAM (p. ej. 200 líneas, siempre activo, costo ~0) + volcado de las últimas N líneas a LittleFS (`/logs.txt`) en cada error/warning y antes de deep-sleep (sobrevive reinicios).
   - Endpoints: `GET /api/logs?n=200&level=warn` (JSON) + visor en el portal con filtro por nivel (INFO/WARN/ERROR), auto-refresh y botón de descarga/limpieza.
   - Incluir SIEMPRE en el log: decisión de descarte de audio (`Frase descartada`, `Abortando sin reproducir`, magia/hz detectados), HTTP status de Gemini/TTS, y causa de wakeup. Sin estos campos, el log no sirve para debugging.
   - Regla: loguear en RAM es gratis; escribir a flash solo en errores o cada N minutos (desgaste de flash).
3. **Gestor de Perfiles y Backup:**
   - Botón para exportar e importar la configuración completa (`config.json`) desde el navegador.
   - Diagnóstico de conexión HTTPS (test directo de ping y latencia hacia los servidores de Google).

---

## 🛠️ Archivos Clave a Modificar
- [`src/wifi_portal.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/wifi_portal.cpp) (endpoints de telemetría, mDNS y dashboard HTML)
- [`include/wifi_portal.h`](file:///C:/Users/admin/Documents/Github/esp32-paper/include/wifi_portal.h) (declaraciones de servidor y mDNS)

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN (Copiar y Pegar)

```text
Hola, hoy vamos a trabajar en la Sesión 6 de Web Dashboard y Telemetría para Copito en ESP32-S3.
Por favor lee primero docs/WORKFLOW_AGENTE.md y docs/roadmap/06_WEB_DASHBOARD_Y_TELEMETRIA.md.

Quiero que nos enfoquemos en:
1. Activar mDNS para que el dispositivo responda en http://copito.local.
2. Añadir un espejo visual del e-Paper en el HTML para poder ver desde el navegador lo que muestra la pantalla.
3. Añadir una consola de logs en vivo en el dashboard para ver las peticiones a Gemini y el estado de la conexión.
4. Compilar, flashear y probar el dashboard desde el navegador web.
```
