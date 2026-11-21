# 🔔 SESIÓN 10 — NOTIFICACIONES Y CONECTIVIDAD AVANZADA

**Dominio:** Conectividad — Integraciones externas sin hardware adicional  
**Prioridad:** 🟢 Baja-Media — Expande los casos de uso de Copito enormemente  
**Prerequisitos:** Portal web funcionando ✅

---

## 🎯 Objetivo de la Sesión

Convertir a Copito en un **nodo activo** de la red local: recibir notificaciones
push desde el celular, integrarse con asistentes del hogar, y alertar sobre eventos.

---

## 📦 Mejoras Planificadas

### A) Endpoint de notificación push (red local)

Cualquier dispositivo en la red puede "despertar" a Copito y hacerle leer
un mensaje en voz alta.

**API:**
```
POST http://copito.local/notify
Content-Type: application/json
{"msg": "Llegó el delivery", "priority": "normal"}
```

**Prioridades:**
- `normal` → Copito termina lo que hace y luego habla
- `urgent` → interrumpe reproducción y habla inmediatamente

**Implementación:**
- Endpoint `/notify` en `setupWebServer()` de `wifi_portal.cpp`
- La placa se despierta del deep sleep al recibir un paquete UDP de broadcast
  (investigar `esp_sleep_enable_wifi_wakeup()` o un wake lock)

---

### B) mDNS `http://copito.local`

En lugar de recordar la IP `192.168.0.176`, acceder siempre por nombre.

**Implementación:** 3 líneas de código con la librería ESPmDNS (ya incluida en Arduino ESP32).
```cpp
#include <ESPmDNS.h>
MDNS.begin("copito");
// Ahora accesible en http://copito.local desde cualquier dispositivo en la red
```

**Archivos afectados:** `src/wifi_portal.cpp` → `startWebServerSTA()`

---

### C) Integración con Home Assistant / MQTT

Si Jorge usa Home Assistant, Copito puede:
- Publicar temperatura/humedad SHTC3 en un topic MQTT
- Suscribirse a topics para recibir comandos ("apagá las luces")
- Ser descubierto automáticamente vía MQTT Discovery

**Librería:** `knolleary/PubSubClient`

---

### D) Webhook de Telegram Bot (alternativa simple)

Sin Home Assistant, un bot de Telegram puede enviar mensajes que Copito lee en voz alta.

**Flujo:**
```
Telegram → Webhook → Router NAT → copito.local/notify → TTS
```
Requiere configuración de port forwarding en el router (o usar un relay como ngrok).

---

### E) Alertas proactivas basadas en tiempo

Copito puede generar alertas sin que nadie le pregunte:
- "Son las 9 de la mañana. ¿Querés un resumen del día?"
- "Hace 3 horas que no hablamos. ¿Todo bien?"
- "La temperatura bajó a 15°C. Quizás quieras cerrar las ventanas."

**Implementación:** tarea FreeRTOS de baja prioridad que evalúa condiciones periódicamente.

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN

```
Retomamos el proyecto Copito AI (ESP32-S3-ePaper-1.54).
Lee primero el workflow en docs/WORKFLOW_AGENTE.md.
Hoy implementaremos la Sesión 10: mDNS copito.local + endpoint /notify para notificaciones push.
Empezamos con mDNS (3 líneas) en startWebServerSTA() de wifi_portal.cpp,
y luego el endpoint /notify que reproduce un mensaje de texto por TTS.
```
