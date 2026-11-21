# 🤖 WORKFLOW MAESTRO DEL AGENTE: COPITO AI
> **Placa:** Waveshare ESP32-S3-ePaper-1.54 (ESP32-S3-PICO-1-N8R8: 8MB Flash, 8MB PSRAM OPI)  
> **Compañero:** Jorge | **Arquitectura:** Firmware C++ Autónomo Direct HTTPS (Zero Intermediate Servers)

Este documento condensa **todo el conocimiento empírico y técnico validado en silicio**. Cualquier sesión de desarrollo futura o agente que trabaje en este repositorio **DEBE consultar y respetar las directivas de este documento**.

---

## 1. ⚡ Mapa de Hardware y Pines Críticos

| Función | Pin GPIO | Modo / Lógica | Observaciones Críticas |
| :--- | :---: | :---: | :--- |
| **User LED (Onboard)** | `GPIO 3` | Salida (Active LOW) | **CUIDADO:** Por defecto es `MTDO` de JTAG. **Siempre** llamar `gpio_reset_pin((gpio_num_t)3)` antes de `pinMode()`. `LOW` = Encendido, `HIGH` = Apagado. Se enciende mientras graba y se apaga al cortar. |
| **Botón BOOT** | `GPIO 0` | Entrada (Active LOW) | Con pull-up interno. Clic corto: despierta / inicia habla. 2do clic: corte manual de habla. Hold 2s: reposo manual. Hold >4s: portal cautivo. |
| **Botón PWR** | `GPIO 18` | Entrada (Active LOW) | Botón de encendido / funciones rápidas. Clic corto conmuta caritas. Hold 2s entra en reposo. |
| **Main Peripheral Rail**| `GPIO 17` | Salida (Active HIGH) | LDO de periféricos generales. Debe encenderse en `setup()` antes de cualquier periférico. |
| **E-Paper Power Gate** | `GPIO 6` | Salida (Active LOW) | P-MOSFET gate para la pantalla de tinta electrónica. `LOW` = Alimentada, `HIGH` = Apagada. |
| **Audio Power Gate** | `GPIO 42` | Salida (Active LOW) | P-MOSFET gate para circuito de audio ES8311. `LOW` = Encendido. |
| **NS4150B PA Enable** | `GPIO 46` | Salida (Active HIGH) | Habilitación del amplificador de 3W. `HIGH` = Activo, `LOW` = Muteado/Sleep. |
| **Battery ADC Sense** | `GPIO 4` | Analógico (ADC1_CH3) | Divisor de tensión a batería LiPo. Usar atenuación `ADC_ATTEN_DB_11`. |
| **I2C SDA / SCL** | `47 / 48` | Bus I2C a 400kHz | Compartido entre ES8311 (`0x18`), RTC PCF85063 (`0x51`) y Sensor SHTC3 (`0x70`). |
| **I2S MCLK** | `GPIO 14` | Master Clock | MCLK generado por ESP32-S3 a 256*Fs. Indispensable para el sintetizador interno del ES8311. |
| **I2S BCLK / WS** | `15 / 38` | Bit Clock / Word Sel | Bus de audio digital sincronizado. |
| **I2S DIN (Mic)** | `GPIO 16` | Data IN <- ES8311 | Retorna datos estéreo donde un canal transporta la señal de micrófono analógico con ganancia. |
| **I2S DOUT (Speaker)**| `GPIO 45` | Data OUT -> ES8311 | Envía muestras al DAC estéreo para amplificación por el NS4150B. |
| **E-Paper SPI** | `12/13/11/10/9/8` | SCK/MOSI/CS/DC/RST/BUSY | Panel e-Ink 200x200 de 1.54" con soporte para refresco parcial ultra rápido (0.3s). |

---

## 2. 🔊 Reglas de Oro del Audio y Codec ES8311

### Calibración del DAC y Control de Volumen
- El registro `0x32` del ES8311 controla el volumen digital del DAC (resolución 0.5 dB/paso).
- **`0xBF` (191 decimal) es exactamente `0 dBFS nominal`** (máxima excursión física y analógica sin clipping digital).
- **Curva perceptual logarítmica implementada (`calc_es8311_volume_reg`):**
  Evita la caída destructiva lineal que atenuaba el 85% a -14.5 dB y el 50% a -48 dB.
  - 100% -> `191` (0.0 dBFS: máxima potencia acústica limpia)
  - 85%  -> `189` (-1.0 dBFS: volumen potente, natural y presente)
  - 80%  -> `187` (-2.0 dBFS)
  - 50%  -> `163` (-14.0 dBFS)
  - 1%   -> `80`  (-55.5 dBFS)
  - 0%   -> `0`   (Mute / -95.5 dB)
- **Chimes y Efectos Acústicos:** Amplitud sintetizada optimizada de 9500 a 28000 (+9.4 dB de ganancia digital limpia) con soft-limiter armónico anti-saturación ante acordes polifónicos.

### Micrófono y Captura
- Micrófono analógico diferencial configurado con **+42 dB de ganancia** (`writeReg(0x16, 0x07)` y volumen digital ADC 200 en `writeReg(0x17, 0xC8)`).
- La lectura I2S se realiza en estéreo. Las muestras del micrófono se detectan dinámicamente en los primeros 128ms comparando la energía de canales L y R.

---

## 3. 🎙️ VAD e Interacción por Voz Fluida (Configurable vía Portal)

La interacción está optimizada para comportamiento natural y todos los tiempos son calibrables en NVS:
1. **Silencio Inicial:** Si el usuario presiona para hablar pero no emite sonido durante el tiempo configurado (`silence_start_ms`, default **2.4 segundos**, rango 0.5s–5.0s), la grabación se cancela rápidamente sin gastar llamadas de API ni batería.
2. **Fin de Habla Automático (VAD):** Cuando el usuario deja de hablar por el tiempo configurado (`silence_end_ms`, default **1.2 segundos**, rango 0.4s–4.0s), la grabación finaliza automáticamente y comienza a procesar.
3. **Duración Máxima:** Configurable (`max_rec_seconds`, default **8 segundos**, rango 3s–30s).
4. **Corte Manual Instantáneo:** Un segundo clic en el botón `BOOT` (GPIO 0) corta la grabación inmediatamente sin esperar el silencio.
5. **Indicador Visual LED:** El LED onboard verde (GPIO 3) se enciende durante todo el tiempo de escucha y se apaga inmediatamente al cortar.

---

## 4. 🧠 Inteligencia Artificial y Streaming TTS

- **Modelo Predeterminado:** `gemini-3.5-flash-lite` (tiempo de respuesta ~1.8s - 2.5s).
- **Modelo de Respaldo:** `gemini-2.5-flash`.
- **Transmisión de Audio sin RAM:** El archivo WAV de voz grabado se codifica a Base64 y se envía por streaming HTTP directo en chunks de 1024 bytes, consumiendo 0 bytes de memoria adicional en el heap.
- **Límite de Tokens:** Configurado en **300 tokens** (`maxOutputTokens: 300`). Permite explicaciones ricas y completas para preguntas complejas.
- **TTS Google Gratuito Multioración:**
  - Endpoint: `translate.google.com/translate_tts?ie=UTF-8&tl=es&client=tw-ob&q=...`
  - Límite por petición: ~150-180 caracteres.
  - **Mecanismo:** El texto se segmenta por signos de puntuación (`. `, `! `, `? `, `, `) en frases de hasta 150 caracteres.
  - Cada frase se descarga secuencialmente en MP3 y se decodifica en tiempo real con `minimp3` en PSRAM, manteniendo el stream de I2S abierto continuamente para evitar pops o silencios perceptibles.

---

## 5. 🛠️ Protocolo de Flasheo y Monitor Serie (Windows)

- La placa entra en modo de reposo profundo tras ~20 segundos de inactividad para proteger la batería, lo cual desconecta el puerto USB CDC (`COM3`).
- **Secuencia de subida:**
  1. Ejecutar el script `scratch/flash_auto.py` (espera activa de hasta 5 minutos).
  2. Pulsar el botón `BOOT` o `RST` en la placa para despertarla.
  3. El script detecta `COM3` automáticamente y sube el firmware en ~20 segundos a 921600 baudios.
  4. Inmediatamente abre el monitor serie para verificación en vivo con DTR/RTS correctamente configurados.

---

## 6. 📋 Checklist para Futuras Modificaciones

- [ ] ¿Se preservaron los pines de hardware y los active-low gates de audio/pantalla?
- [ ] ¿La calibración de volumen digital es coherente con el rango 0-255 del ES8311?
- [ ] ¿El prompt del sistema mantiene el tono de compañero para Jorge sin palabras infantiles?
- [ ] ¿Las funciones que usan memoria grande reservan en PSRAM (`ps_malloc` / `psramFound()`)?
- [ ] ¿La pantalla e-Paper refresca en Core 0 para no interferir con las tareas de audio en Core 1?
- [ ] ¿El código compila limpiamente con `pio run -e esp32-s3-paper`?

---

## 7. 🗺️ Roadmap de Sesiones Temáticas
Para continuar el desarrollo de forma estructurada y enfocada, consulta el roadmap modular:
- [**Índice Maestro de Sesiones**](file:///C:/Users/admin/Documents/Github/esp32-paper/docs/roadmap/README.md) (Sesiones 01 a 10 con prompts de arranque directo).
