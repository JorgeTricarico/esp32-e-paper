# 🤖 Asistente Autónomo ESP32-S3 E-Ink (Waveshare 1.54")

Firmware modular de alto rendimiento para el desarrollo de un asistente de voz autónomo con rostro expresivo en pantalla de tinta electrónica (E-Paper). Diseñado específicamente para la placa **Waveshare ESP32-S3-ePaper-1.54** (ESP32-S3 con 16 MB Flash, PSRAM Octal, códec de audio ES8311, amplificador NS4150B y pantalla e-Paper de 200x200 píxeles).

---

## 🌟 Características Principales

* **Cero Servidor Intermedio (100% Standalone):** La placa se comunica directamente por HTTPS con las APIs públicas (Google Gemini 3.8 Flash / 3.5 Flash y ElevenLabs / OpenAI TTS) sin depender de servidores proxy o Python en la red local.
* **Inteligencia Multimodal (Google Gemini 3.8 Flash):** El modelo de última generación de Google procesa la voz del niño y genera respuestas inmediatas, tiernas y breves de 1-2 oraciones.
* **Streaming de Audio TTS Directo (I2S):** Streaming directo de audio PCM (16 kHz / 24 kHz) desde ElevenLabs u OpenAI TTS directo al amplificador NS4150B y parlante.
* **Rostro Expresivo Avanzado en E-Ink (Dithering Bayer + Refresco Parcial <300ms):**
  * **Ilustración Detallada:** Ojos multicapa con brillo cristalino, sombreado por tramado Bayer (dithering) de 16 niveles de gris y chasis estilizado de robot compañero.
  * **Micro-Movimientos Iterativos:** Simulación de vida con pestañeo suave (4 fotogramas de párpado), seguimiento de mirada (desplazamiento de pupilas en reposo) y fonemas labiales reales (O, A, E, Rest) durante la voz.
* **Simulación Local Disponible:** Incluye un emulador visual completo en navegador web ([`simulator/index.html`](file:///C:/Users/admin/Documents/Github/esp32-paper/simulator/index.html)) y configuración para Wokwi.
* **Gestión de Red Dual (Multi-WiFi + Portal Cautivo):**
  * Conexión automática con `WiFiMulti` a las redes configuradas del usuario.
  * Si la red falla o se mantiene presionado el botón **BOOT (GPIO 0)** durante 4 segundos, levanta un Access Point (`ESP32-Paper-Assistant` en `192.168.4.1`) con portal web para escanear redes, guardar API Keys y realizar actualizaciones de firmware inalámbricas (**OTA**).

---

## 📐 Asignación de Pines de Hardware (Pinout)

### 1. Pantalla E-Paper (SPI 200×200)
> **IMPORTANTE:** El pin de alimentación de la pantalla (`PIN_EPD_PWR`) es **Activo en BAJO (Active LOW)**. Debe colocarse en `LOW (0)` para energizar la pantalla.

| Señal | GPIO | Descripción |
|---|---|---|
| `EPD_PWR` | **GPIO 6** | Control de alimentación (LOW = Encendido) |
| `EPD_BUSY` | **GPIO 8** | Estado de pantalla (HIGH = Ocupada) |
| `EPD_RST` | **GPIO 9** | Reset de pantalla |
| `EPD_DC` | **GPIO 10** | Comando (LOW) / Datos (HIGH) |
| `EPD_CS` | **GPIO 11** | Chip Select SPI |
| `EPD_SCK` | **GPIO 12** | Reloj SPI |
| `EPD_MOSI` | **GPIO 13** | Datos SPI (SDI) |

### 2. Códec de Audio ES8311 & Amplificador NS4150B
> **IMPORTANTE:** El pin de alimentación del circuito de audio (`PIN_AUDIO_PWR`) es **Activo en BAJO (Active LOW)**. Debe colocarse en `LOW (0)` para alimentar el códec y amplificador.

| Señal / Bus | GPIO | Descripción |
|---|---|---|
| `Audio_PWR` | **GPIO 42** | Alimentación Códec y Amplificador (LOW = Encendido) |
| `I2C SDA` | **GPIO 47** | Bus de control I2C (Dirección `0x18`) |
| `I2C SCL` | **GPIO 48** | Reloj I2C |
| `I2S MCLK` | **GPIO 14** | Master Clock generado por el ESP32-S3 |
| `I2S BCLK` | **GPIO 15** | Bit Clock I2S |
| `I2S WS (LRCK)` | **GPIO 38** | Word Select I2S |
| `I2S DOUT` | **GPIO 45** | Datos hacia Parlante (ES8311 DSDIN) |
| `I2S DIN` | **GPIO 16** | Datos desde Micrófono (ES8311 ASDOUT) |

### 3. Botones y Periféricos
| Señal | GPIO | Descripción |
|---|---|---|
| `BOOT_BUTTON` | **GPIO 0** | Botón físico de usuario (Pulsación corta = Hablar, Larga = Portal) |
| `PWR_BUTTON` | **GPIO 18** | Botón de encendido |
| `VBAT_ADC` | **GPIO 17** | Medición de batería |

---

## 📁 Estructura del Código Modular

```
esp32-paper/
├── platformio.ini         # Configuración completa de compilación (ESP32-S3, 8MB Flash, 8MB PSRAM OPI)
├── default_8MB.csv        # Tabla de particiones extendida (Dual OTA de 3.5MB cada una)
├── include/
│   ├── config.h           # Definición de pines, constantes, modelos y structs
│   ├── faces_bitmaps.h    # Renderizador procedural de caritas 200x200
│   ├── epaper_driver.h    # Controlador de pantalla e-Paper con LUTs de refresco parcial
│   ├── audio_es8311.h     # Driver I2C del ES8311 y motor de audio I2S (Grabación / Reproducción)
│   ├── wifi_portal.h      # Administrador Multi-WiFi, DNS y Portal Cautivo Web con OTA
│   └── ai_engine.h        # Cliente HTTPS para Gemini y streaming directo de voz TTS
├── src/
│   ├── epaper_driver.cpp  # Implementación de rutinas SPI y refresco parcial (<350ms)
│   ├── audio_es8311.cpp   # Inicialización de registros ES8311 y streaming de audio I2S
│   ├── wifi_portal.cpp    # Servidor Web móvil, escaneo de redes y actualización OTA
│   ├── ai_engine.cpp      # Llamadas REST a Google AI Studio y ElevenLabs / OpenAI
│   └── main.cpp           # Máquina de estados, detección de botón y ciclo principal
└── scripts/
    ├── flash_tool.bat     # Lanzador de 1-click para Windows
    └── flash_tool.ps1     # Script de detección de COM, compilación y flasheo autónomo
```

---

## 🚀 Guía de Compilación y Flasheo

### Opción 1: Ejecución en 1-Click (Recomendada)
Haz doble clic en el archivo [`scripts/flash_tool.bat`](file:///C:/Users/admin/Documents/Github/esp32-paper/scripts/flash_tool.bat).
El script automáticamente:
1. Detectará el puerto COM donde está enchufado el ESP32-S3.
2. Compilará el firmware con las particiones de 16 MB.
3. Subirá el firmware al ESP32-S3.
4. Abrirá el monitor serie a 115200 baudios.

### Opción 2: Usando PlatformIO CLI
Abre PowerShell o terminal en la raíz del proyecto y ejecuta:
```bash
# Compilar el proyecto
pio run -e esp32-s3-paper

# Subir al ESP32-S3
pio run -e esp32-s3-paper -t upload

# Abrir el monitor serie
pio device monitor -b 115200
```

### Opción 3: Usando VS Code
1. Abre la carpeta `esp32-paper` en VS Code.
2. Asegúrate de tener la extensión **PlatformIO IDE** instalada.
3. En la barra inferior de PlatformIO, haz clic en **Build** (✓) y luego en **Upload** (→).

---

## ⚙️ Primera Configuración (Portal Web)

1. Al encender por primera vez (o tras mantener presionado el botón **BOOT** durante 4 segundos), la pantalla mostrará el rostro de **MODO RESCATE**.
2. Conéctate desde tu teléfono móvil o notebook a la red WiFi:
   * **SSID:** `ESP32-Paper-Assistant`
   * **Contraseña:** *(Abierta, sin contraseña)*
3. El portal cautivo se abrirá automáticamente (o ingresa a `http://192.168.4.1` en tu navegador).
4. Configura:
   * Tu red WiFi doméstica (SSID y Contraseña).
   * Tu **API Key de Google Gemini** ([Google AI Studio](https://aistudio.google.com/)).
   * Tu proveedor y clave de **TTS** (ElevenLabs u OpenAI).
   * Volumen deseado (0-100%).
5. Presiona **Guardar y Reiniciar**. ¡Copito se conectará a tu WiFi y estará listo para hablar!

---

## 🎮 Modo de Uso

* **Hablar con Copito:** Presiona brevemente el botón **BOOT (GPIO 0)**. 
  * La pantalla cambiará a `LISTENING` y sonará un tono ascendente.
  * Habla durante 3 o 4 segundos.
  * Al terminar, pasará a `THINKING` mientras consulta a Gemini.
  * Copito responderá con voz por el parlante mientras su boca se mueve (`SPEAKING`).
  * Al finalizar, volverá a descansar con una sonrisa (`IDLE`).
* **Forzar Portal de Configuración:** Mantén presionado el botón **BOOT** durante 4 segundos.
