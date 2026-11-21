# Libro de Animales Interactivo — ESP32-S3 e-Paper 1.54"

Juego educativo infantil para la placa **Waveshare ESP32-S3-ePaper-1.54** (`ESP32-S3-PICO-1-N8R8`, 8 MB Flash, 8 MB PSRAM OPI, pantalla 200x200 px 1-bit y códec de audio **ES8311**).

## Características
- **8 animales iniciales:** `PERRO`, `GATO`, `VACA`, `PATO`, `LEÓN`, `OVEJA`, `SAPO`, `ELEFANTE`.
- **Cero parpadeo al animar:** Al pulsar `BOOT`, reproduce el sonido por el parlante (Core 0) y anima únicamente la Zona 2 (`x=16, y=24, w=168, h=128`) mediante refresco parcial (`Frame A -> Frame B -> Frame A`), ignorando pulsaciones repetidas mientras suena.
- **Cambio rápido de animal:** Pulsación corta de `PWR` (< 1.5 s) avanza al siguiente animal con refresco parcial (`0.3 s`), ejecutando 1 refresco completo automático cada 6 cambios para eliminar *ghosting*.
- **Apagado y ahorro de batería:** Pulsación larga de `PWR` (>= 3 s) muestra `"¡HASTA PRONTO!"` y entra en Deep Sleep liberando `PIN_MAIN_PWR`. Tras 3 minutos de inactividad entra en Deep Sleep manteniendo el último animal en pantalla.

---

## Compilar y Flashear (PlatformIO)

Desde la carpeta `projects/esp32-animales-epaper/`:

```powershell
# 1. Compilar el firmware
pio run -e esp32-s3-paper

# 2. Subir el firmware al ESP32-S3
pio run -e esp32-s3-paper -t upload

# 3. Subir la particion LittleFS con los audios (/sounds/*.wav)
pio run -e esp32-s3-paper -t uploadfs

# 4. Abrir monitor serie
pio device monitor -b 115200
```

> **Nota de Deep Sleep:** Si la placa está dormida y no aparece el puerto USB CDC, mantén presionado `BOOT` o pulsa `RST` al iniciar el upload.

---

## Reemplazar Audios Placeholder por Sonidos Reales

Por defecto `tools/prepare_audio.py` genera audios sintetizados en `data/sounds/` para poder probar desde el primer minuto. Para usar sonidos reales:

1. Coloca tus archivos (`.mp3`, `.wav`, `.ogg`, `.flac` o `.m4a`) en `assets/raw_audio/` con el ID del animal (`perro.mp3`, `gato.wav`, `leon.mp3`, etc.).
2. Ejecuta:
   ```powershell
   python tools/prepare_audio.py
   ```
   El script recorta silencios iniciales, normaliza a `-3 dBFS`, recorta a máximo `2.5 s` y convierte a **WAV PCM 16 kHz, 16-bit, Mono** en `data/sounds/`.
3. Vuelve a subir la partición de archivos:
   ```powershell
   pio run -e esp32-s3-paper -t uploadfs
   ```

---

## Personalizar Sprites o Agregar Nuevos Animales

- **Sobrescribir dibujos:** Coloca `assets/custom_sprites/<id>_a.png` y `<id>_b.png` y ejecuta `python tools/generate_graphics.py`.
- **Agregar un animal nuevo:**
  1. Añade la tupla `(id, "NOMBRE", "¡ONOMATOPEYA!")` a `ANIMALS` en `tools/generate_graphics.py` (y su función de dibujo o PNGs en `assets/custom_sprites/`).
  2. Añade el `id` a `ANIMAL_IDS` en `tools/prepare_audio.py`.
  3. Ejecuta ambos scripts, revisa `tools/preview_sheet.png` y vuelve a compilar/subir firmware + `uploadfs`.

---

## Ejecutar Pruebas (Host)

```powershell
pytest tests/test_assets_and_firmware.py -v
```
