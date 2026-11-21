# 📄 ESP32-S3 e-Paper — Repositorio Multi-Proyecto

Monorepo de firmwares para la placa **Waveshare ESP32-S3-ePaper-1.54** (ESP32-S3, 8MB/16MB Flash, 8MB PSRAM OPI, pantalla e-Paper 200x200, códec de audio ES8311 + NS4150B, sensor SHTC3 y RTC PCF85063).

---

## 📁 Estructura del Repositorio

```
esp32-paper/
├── README.md                  # Este archivo
├── projects/
│   ├── copito/                # Asistente de voz autónomo con IA (Gemini + TTS + e-Paper)
│   └── _template/             # Plantilla base lista para clonar y crear nuevos proyectos
└── ...
```

---

## 📦 Proyectos Disponibles

| Proyecto | Ruta | Descripción |
| :--- | :--- | :--- |
| **Copito AI** | [`projects/copito/`](projects/copito/README.md) | Asistente de voz autónomo con rostro expresivo en e-Paper, Gemini AI, streaming TTS, reloj, clima, cuentos y portal cautivo OTA. |
| **Plantilla Base** | [`projects/_template/`](projects/_template/README.md) | Skeleton mínimo con la configuración de PlatformIO, particiones y pinout de la placa Waveshare ESP32-S3-ePaper-1.54. |

---

## 🚀 Cómo Compilar y Flashear un Proyecto

Cada carpeta dentro de `projects/<nombre>/` es un proyecto PlatformIO independiente.

```powershell
# Ir a la carpeta del proyecto deseado
cd projects\copito

# Compilar
pio run -e esp32-s3-paper

# Flashear por USB
pio run -e esp32-s3-paper -t upload

# Abrir monitor serie
pio device monitor -b 115200
```

> **Nota sobre Deep Sleep:** En firmwares con ahorro de energía (como Copito), la placa desconecta el puerto USB CDC (`COM3`) al entrar en deep sleep. Pulsa el botón `BOOT` o `RST` antes/durante el flasheo para despertarla.

---

## ➕ Cómo Crear un Nuevo Proyecto

1. Copia la plantilla base:
   ```powershell
   Copy-Item -Recurse projects\_template projects\mi-nuevo-proyecto
   ```
2. Edita `projects\mi-nuevo-proyecto\src\main.cpp` y `platformio.ini` según tus necesidades.
3. Compila y flashea desde `projects\mi-nuevo-proyecto\`.
