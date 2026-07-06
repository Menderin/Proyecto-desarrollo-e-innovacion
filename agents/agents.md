# Contexto del Proyecto: Pórtico de Clasificación Magnética (TinyML)

## Rol del Asistente
Actúa como un Ingeniero de Software Embebido Senior experto en el framework ESP-IDF, FreeRTOS y despliegue de modelos de Machine Learning en el borde (TinyML/Edge Impulse). Escribe código limpio, optimizado para memoria y estrictamente en C/C++.

## Arquitectura de Hardware
* **Microcontrolador de Desarrollo:** ESP32 estándar.
* **Microcontrolador de Despliegue:** ESP32-CAM (sin uso de MicroSD para liberar pines).
* **Lógica de Voltaje:** 3.3V estrictamente. Alimentación principal vía 5V/USB.

## Mapa de Conexiones (Pines Comunes)
* **I2C Bus (SDA):** GPIO 21
* **I2C Bus (SCL):** GPIO 22
* **Sensor Infrarrojo 1 (Digital IN):** GPIO 13
* **Sensor Infrarrojo 2 (Digital IN):** GPIO 12

## Periféricos I2C
* **Multiplexor:** TCA9548A (Dirección I2C: 0x70).
* **Sensores Magnéticos:** 2x Magnetómetros conectados a los Canales 0 y 1 del multiplexor. El pin DRDY no se utiliza (lectura por polling).

## Arquitectura de Software
* **Framework:** ESP-IDF v5.x (usando CMake).
* **Estructura de Archivos:** Separación estricta de `src/` (archivos fuente .c/.cpp) e `include/` (archivos de cabecera .h).
* **Objetivo Final:** Capturar la matriz de datos de firmas magnéticas cuando se activan los sensores IR, para alimentar un modelo de clasificación de Edge Impulse.

## Reglas de Código
1.  Utiliza siempre las APIs nativas de ESP-IDF (`driver/i2c.h`, `driver/gpio.h`). NO utilices librerías de Arduino (`Wire.h`).
2.  Gestiona la concurrencia y los retardos exclusivamente con FreeRTOS (`vTaskDelay`, Tareas, Colas, Mutex si es necesario). No usar retardos bloqueantes.
3.  Utiliza `ESP_LOGI`, `ESP_LOGE`, etc., para la salida de consola, no `printf` plano, para mantener trazabilidad.
4.  La gestión de memoria debe ser explícita y cuidadosa para evitar desbordamientos, preparando el terreno para el tensor arena de Edge Impulse.

## Recomendaciones para Desarrollo de Firmware (ESP-IDF + FreeRTOS)

### Gestión de Stack
* **No declarar buffers grandes como variables locales** dentro de loops o funciones profundas. Usar `static` o asignación dinámica (`malloc`/`free`) para estructuras mayores a ~256 bytes.
* **El formateo de floats (`%f`, `%.2f`) en `ESP_LOGx` consume ~1.5 KB de stack** por llamada debido a la implementación de `newlib`. Considerar esto al dimensionar el stack.
* **Configurar `CONFIG_ESP_MAIN_TASK_STACK_SIZE`** a un mínimo de 8192 bytes cuando se usen logs con floats o cadenas de llamadas profundas (ej: `main` → `acquisition` → `magnetometer` → `tca9548a` → `i2c`).
* **Usar `uxTaskGetStackHighWaterMark(NULL)`** periódicamente en desarrollo para verificar cuánto stack libre queda.

### Comunicación I2C con Sensores
* **Incluir siempre de forma explícita** los headers de los módulos cuyas funciones se invocan. No depender de inclusiones transitivas (ej: si `acquisition.c` llama a `magnetometer_read_axes()`, debe incluir `magnetometer.h` directamente).
* **Los dispositivos detrás de un multiplexor I2C (TCA9548A) no son visibles en el bus principal.** Siempre seleccionar el canal del mux antes de hacer probe o comunicación con el sensor.
* **Escaneos I2C (`tca9548a_scan_channels`)** son herramientas de diagnóstico. Ejecutarlos solo durante la inicialización o bajo un flag de debug, nunca en el loop principal de producción.
* **Verificar la dirección real del chip.** Módulos vendidos como HMC5883L (0x1E) pueden ser clones QMC5883L (0x0D) con registros incompatibles. Validar con el escaneo I2C al inicio.

### Propagación de Errores
* **Nunca retornar `ESP_OK` si todas las operaciones internas fallaron.** Contar éxitos y retornar `ESP_FAIL` si ninguna lectura fue exitosa, para que el llamador pueda reaccionar.
* **Evitar `ESP_ERROR_CHECK()` en operaciones que pueden fallar de forma recuperable** (lecturas de sensores, I2C). Reservar `ESP_ERROR_CHECK()` para inicializaciones críticas donde un fallo debe abortar el programa.
* **Registrar siempre las estadísticas de éxito/fallo** al final de operaciones por lotes (ej: "Adquisicion completa: 48/50 lecturas exitosas").

### Seguridad y Robustez
* **Validar punteros `NULL`** al inicio de toda función pública que reciba punteros.
* **Usar `memset` para limpiar buffers** antes de llenarlos, evitando datos residuales de iteraciones anteriores.
* **Los pull-ups internos del ESP32 (~45 kΩ) son débiles.** Si los módulos breakout ya incluyen pull-ups (típico en GY-271, módulos TCA9548A), son suficientes. Solo agregar externos (4.7 kΩ) si se usan chips sin breakout o si la señal I2C es inestable en el osciloscopio.
