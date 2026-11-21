# 🔋 Sesión 5: Gestión de Energía, Batería y Autonomía Extrema

> **Objetivo de la sesión:** Maximizar la duración de la batería de litio (semanas de duración en reposo e-Paper) apagando raíles de alimentación selectivos y calibrando la curva de descarga.

---

## 🎯 Metas Específicas
1. **Calibración Precisa de la Batería (GPIO 4 ADC):**
   - Muestreo con sobremuestreo (oversampling) y filtrado de media móvil para evitar lecturas ruidosas.
   - Tabla de descarga LiPo real (4.20V = 100%, 3.85V = 50%, 3.60V = 15%, 3.30V = 0%).
   - Alerta acústica y visual cuando la batería caiga por debajo del 10%.
2. **Estrategia de Apagado de Raíles (Power Gating):**
   - En reposo: apagar circuito de audio (`GPIO 42 = HIGH`), apagar amplificador (`GPIO 46 = LOW`), y apagar pantalla e-Paper (`GPIO 6 = HIGH`). La pantalla e-Paper mantiene su imagen permanentemente con 0W de consumo.
   - Reducir consumo en reposo a menos de 45 microamperios (Deep Sleep con wakeup por botón BOOT en GPIO 0 o RTC PCF85063).
3. **Modo "Vigilancia / Light Sleep":**
   - Transición a Light Sleep automático entre frases con despertar por timer o botón.

---

## 🛠️ Archivos Clave a Modificar
- [`src/main.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/main.cpp) (rutinas de sueño, apagado de periféricos)
- [`include/config.h`](file:///C:/Users/admin/Documents/Github/esp32-paper/include/config.h) (constantes de batería y timers de inactividad)

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN (Copiar y Pegar)

```text
Hola, hoy vamos a trabajar en la Sesión 5 de Energía, Batería y Autonomía para Copito en ESP32-S3.
Por favor lee primero docs/WORKFLOW_AGENTE.md y docs/roadmap/05_ENERGIA_BATERIA_Y_AUTONOMIA.md.

Quiero que nos enfoquemos en:
1. Calibrar la lectura del voltaje de batería en el ADC de GPIO 4 con curva real de descarga LiPo.
2. Optimizar el apagado completo de los raíles de energía (audio, amplificador, pantalla) en el modo reposo.
3. Configurar el RTC para que pueda despertar la placa a intervalos programados o al pulsar el botón BOOT.
4. Compilar y verificar el comportamiento de entrada y salida del modo reposo profundo.
```

---

## 📊 Auditoría 2026-09-09 (medida en código, sin medidor USB aún)

- WiFi `setSleep(false)` siempre + CPU 240 MHz siempre + reloj que nunca duerme
  (poll 50 ms, re-render + 5000 B SPI por minuto). Radio+CPU ≈ 70-80% del gasto.
- E-paper ya gasta ~0 entre refrescos (deep sleep 0x10 + corte de raíl GPIO 6).
- Hora sin internet ya funciona entre syncs (RTC PCF85063); el costo no es el
  NTP por tick sino mantenerse despierto con WiFi full.
- "Partial" actual = solo waveform rápida, ventana siempre completa.

## 🟢 Nivel LEVE — implementado 2026-09-09 (sin cambio visible)

1. `powerBoost()` (radio full) / `powerRelax()` (modem-sleep salvo
   portal/hold/check-in/voz) en `src/main.cpp`; gancho en `changeState()`.
2. `streamTtsToSpeaker()`, música/radio en vivo (`PowerStreamGuard`) y
   descarga pull-OTA envueltos en boost/relax.
3. Tope TX `WIFI_POWER_11dBm` al conectar (hogar con RSSI -60s).
4. NTP 6 h → 24 h (el RTC aguanta la deriva).
5. Reloj: hora máx 1 vez/s, BOOT a 10 Hz; clima `delay(50)` → 100 ms.
6. ⚠️ Escalado CPU a 80 MHz RETIRADO el mismo día: colgaba el equipo
   (ping vivo, HTTP/serie muertos) con OPI PSRAM. Reevaluar con medidor
   USB y test de estabilidad antes de reintentarlo.
7. Pendiente de medir con medidor USB: baseline idle 10 min, reloj 1 h, noche %.

## 🟡 Nivel MEDIO — pendiente (costo: latencias chicas)

1. Partial real por ventana: solo dígitos que cambian (SSD1681 0x44/0x45).
2. Light sleep entre ticks del reloj (wake en ms, RAM intacta).
3. I2S/mic/SPI off en idle/reloj; re-init bajo demanda (~100 ms).
4. Animación idle 6 s solo si hubo interacción reciente.
5. Check-in 30 → 60 min por defecto.

## 🔴 Nivel ALTO / modo viaje — pendiente (costo: funciones muertas)

1. Reloj offline: WiFi off total, hora solo RTC, deep sleep entre minutos con
   wake por timer; NTP 1 vez/día. Full-refresh limpieza 15 min → 1 h.
2. Costo: portal y botones muertos mientras duerme (solo BOOT despierta),
   sin voz hasta despertar, granularidad de 1 minuto.
