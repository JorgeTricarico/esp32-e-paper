# 🎨 Sesión 2: Interfaz Visual, Expresiones y Pantalla E-Paper

> **Objetivo de la sesión:** Transformar la pantalla de tinta electrónica de 1.54" (200x200) en una interfaz rica, expresiva, con animaciones fluidas, burbujas de texto adaptativas y widgets informativos en tiempo real.

---

## 🎯 Metas Específicas
1. **Sistema de Expresiones y Animaciones Faciales:**
   - Crear un catálogo ampliado de emociones (Curioso, Pensativo, Sorprendido, Alegre, Guiño, Dormilón, Concentrado).
   - Micro-animación de parpadeo suave y periódico durante el estado de reposo (`IDLE`), utilizando refresco parcial ultra rápido (~300ms) sin flicker.
2. **Burbujas de Diálogo y Tipografía Adaptativa:**
   - Tipografía proporcional optimizada con auto-escalado de fuentes (fuente grande para frases cortas, fuente compacta para respuestas más largas).
   - Renderizado limpio de iconos vectoriales / emojis monocromáticos (sol, estrella, corazón, libro, nube, bombilla de idea).
3. **Barra de Estado y Widgets Inteligentes:**
   - Top Bar informativa con reloj sincronizado por RTC PCF85063 o NTP (`HH:MM`).
   - Temperatura y humedad ambiental en tiempo real leídas del sensor SHTC3 en bus I2C (`23°C | 55%`).
   - Icono visual de batería (con 4 niveles de carga calculados desde el ADC de GPIO 4) y estado de señal WiFi.

---

## 🛠️ Archivos Clave a Modificar
- [`include/faces_bitmaps.h`](file:///C:/Users/admin/Documents/Github/esp32-paper/include/faces_bitmaps.h) (bitmaps de expresiones y algoritmos de renderizado)
- [`src/epaper_driver.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/epaper_driver.cpp) (control de refresco parcial vs full)
- [`src/main.cpp`](file:///C:/Users/admin/Documents/Github/esp32-paper/src/main.cpp) (lectura de RTC y SHTC3 para la pantalla)

---

## 🚀 PROMPT DE ARRANQUE PARA LA SESIÓN (Copiar y Pegar)

```text
Hola, hoy vamos a trabajar en la Sesión 2 de UI y Visuales E-Paper para Copito en ESP32-S3.
Por favor lee primero docs/WORKFLOW_AGENTE.md y docs/roadmap/02_UI_VISUALES_Y_EPAPER.md.

Quiero que nos enfoquemos en:
1. Enriquecer las expresiones de la cara de Copito con nuevas miradas y animación de parpadeo suave en reposo.
2. Mejorar la barra superior (TopBar) mostrando la hora actual, temperatura/humedad del sensor SHTC3 y el nivel de batería real.
3. Mejorar el renderizado de texto en la pantalla e-Ink para que se lea impecable con bordes nítidos y formato adaptativo.
4. Compilar y flashear para verificar cómo se ven las nuevas caritas y widgets en la pantalla física.
```
