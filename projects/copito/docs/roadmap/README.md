# 🗺️ ROADMAP DE DESARROLLO MODULAR: COPITO AI

> ⭐ **Nuevo orden de trabajo:** [**00_PLAN_PRIORIZADO.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/00_PLAN_PRIORIZADO.md) — lo pendiente ordenado por lo que más se ve / menos batería gasta. Empezar por ahí.

Este directorio contiene la planificación estratégica para evolucionar exponencialmente a **Copito AI** en sesiones de trabajo individuales, estructuradas y enfocadas en un solo dominio a la vez.

---

## 📚 Índice de Sesiones Temáticas

| # | Documento | Dominio de Enfoque | Impacto Clave |
| :---: | :--- | :--- | :--- |
| **01** | [**01_PERFORMANCE_Y_LATENCIA.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/01_PERFORMANCE_Y_LATENCIA.md) | Latencia y Memoria | Respuestas de voz en < 1.8s, reutilización de sockets TLS y optimización de PSRAM. |
| **02** | [**02_UI_VISUALES_Y_EPAPER.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/02_UI_VISUALES_Y_EPAPER.md) | Visuales y E-Paper | Expresiones animadas, parpadeo suave, widgets de reloj RTC y sensor SHTC3. |
| **03** | [**03_AUDIO_Y_CODEC_ES8311.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/03_AUDIO_Y_CODEC_ES8311.md) | Acústica y Sonido | Filtros ecualizadores de parlante 3W, noise gate en mic y soundscapes retro-futuristas. |
| **04** | [**04_IA_MEMORIA_Y_FUNCTION_CALLING.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/04_IA_MEMORIA_Y_FUNCTION_CALLING.md) | IA y Function Calling | Memoria a largo plazo sobre Jorge y control de hardware por voz (temperatura, dibujos). |
| **05** | [**05_ENERGIA_BATERIA_Y_AUTONOMIA.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/05_ENERGIA_BATERIA_Y_AUTONOMIA.md) | Batería y Energía | Curva LiPo real, apagado selectivo de raíles de energía y semanas de autonomía en reposo. |
| **06** | [**06_WEB_DASHBOARD_Y_TELEMETRIA.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/06_WEB_DASHBOARD_Y_TELEMETRIA.md) | Web & Conectividad | mDNS `http://copito.local`, espejo de pantalla e-Paper en HTML y telemetría en vivo. |
| **07** | [**07_INTERACCION_Y_WAKE_WORD.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/07_INTERACCION_Y_WAKE_WORD.md) | UX e Interacción | Multi-gestos en botón BOOT (repetir, modos, sleep) y Wake Word "Hey Copito" con WakeNet. |
| **08** | [**08_SENSORES_Y_OFFLINE.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/08_SENSORES_Y_OFFLINE.md) | Sensores y Offline | Sensor ambiental SHTC3 (Temp/Hum), reloj RTC PCF85063 y respuestas locales instantáneas. |
| **09** | [**09_MEMORIA_Y_CONTEXTO.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/09_MEMORIA_Y_CONTEXTO.md) | Memoria y Personalidad | Buffer circular en PSRAM para historial de conversación y memoria a largo plazo en NVS. |
| **10** | [**10_NOTIFICACIONES_Y_CONECTIVIDAD.md**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/10_NOTIFICACIONES_Y_CONECTIVIDAD.md) | Conectividad y Alertas | Push notifications vía `/notify`, mDNS `copito.local`, integración MQTT y alertas proactivas. |

---

## 🧭 ¿Cómo iniciar una sesión de trabajo?

1. Elige el tema que desees mejorar según tu prioridad del día.
2. Abre el archivo correspondiente en `docs/roadmap/`.
3. Copia el bloque **"PROMPT DE ARRANQUE PARA LA SESIÓN"** que viene al final de cada documento.
4. Pégalo directamente en la conversación con el asistente para que arranque la sesión enfocado y con todo el contexto técnico cargado.
