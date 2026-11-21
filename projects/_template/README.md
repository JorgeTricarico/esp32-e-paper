# Plantilla Base — Waveshare ESP32-S3-ePaper-1.54

Proyecto mínimo de arranque para nuevos firmwares sobre la placa **Waveshare ESP32-S3-ePaper-1.54**, con soporte base de **OTA de ultra-bajo consumo**.

## OTA con Decaimiento Exponencial (Base Estándar)
Para proyectos donde el WiFi no es la función principal:
- En uso normal y en reposo el WiFi permanece **apagado**.
- Cuando cambia el firmware compilado (`ESP.getSketchMD5()`), al dormir programa despertares silenciosos cada **10 min durante la primera hora**.
- Luego aplica backoff exponencial (`20 → 40 → 80 → 160 → 320 → 720 min`) hasta quedar estable y **desactivar los check-ins automáticos** para consumo cero de WiFi.

## Uso

```powershell
# Compilar y subir por USB (primera vez)
pio run -e esp32-s3-paper -t upload

# Dejar esperando un flash OTA en la PC
python tools/ota_stage_server.py
```
