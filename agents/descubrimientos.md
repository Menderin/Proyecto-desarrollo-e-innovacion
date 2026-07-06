# Descubrimientos y Depuración: Problema de Timeout en I2C

Este documento registra los hallazgos y el proceso de diagnóstico de hardware/software realizado al enfrentarnos a un fallo de comunicación en el bus I2C del proyecto del pórtico de clasificación magnética.

## El Problema
Durante la inicialización del proyecto (ESP32 con ESP-IDF v5.x), el sistema fallaba constantemente al intentar comunicarse con el multiplexor PCA9548A.
El log mostraba de forma persistente el error:
`E (...) APP: TCA9548A NO detectado (0x70) err=ESP_ERR_TIMEOUT`

Además, al realizar un escaneo en todo el bus (direcciones `0x08` a `0x77`), el ESP32 no encontraba ningún dispositivo.

## Diferencia Clave entre Errores I2C en ESP32
- **`ESP_FAIL` o `ESP_ERR_NOT_FOUND`:** Significa que el ESP32 emitió las señales eléctricas correctamente, pero ningún esclavo (sensor/módulo) respondió con un "ACK" (Acknowledge) en esa dirección. Es un NACK limpio.
- **`ESP_ERR_TIMEOUT`:** Indica que la transacción ni siquiera pudo completarse a nivel eléctrico. Significa que el bus I2C está físicamente **atascado** (las líneas SDA o SCL retenidas en 0V) o la capacitancia es tan alta que los flancos de reloj no suben a tiempo. 

## Proceso de Diagnóstico y Pruebas
1. **Verificación de Software:**
   - Se analizó `main.c`, `app_config.h` e `i2c_bus.c`. 
   - Se validó que los pines (SDA=21, SCL=22), las pull-ups internas, la frecuencia (50kHz / 100kHz) y los tiempos de espera de la API de ESP-IDF estaban programados perfectamente. El problema NO era el código.

2. **Verificación del PCA9548A (Módulo Multiplexor):**
   - **Direccionamiento:** Se confirmó A0, A1, y A2 a GND para asegurar la dirección `0x70`.
   - **Pull-ups:** El módulo contaba con resistencias de 10kΩ (`103`).
   - **Pin RESET:** Es un pin activo en BAJO. Se comprobó conectándolo a 3.3V (VCC) para evitar que mantuviera el chip paralizado, pero el problema persistía.

3. **La "Prueba del Vacío" (Aislamiento de la falla):**
   - Al desconectar totalmente el PCA9548A y dejar los pines GPIO 21 y 22 al aire, el escáner del código arrojó un error `ESP_FAIL` limpio, en vez de un `ESP_ERR_TIMEOUT`. 
   - **Conclusión:** Los pines del ESP32 estaban completamente funcionales; el módulo multiplexor era el que arrastraba el bus a tierra cuando se conectaba.

4. **La Prueba Directa (Confirmación de sensores):**
   - Se conectó un solo magnetómetro (HMC5883L) directo a los pines 21 y 22.
   - El escaneo encontró instantáneamente el dispositivo en la dirección `0x1E`.
   - **Conclusión:** Los magnetómetros son genuinos (HMC y no clones QMC que operan en 0x0D), funcionan con el ESP32, y la infraestructura de código base para leer I2C está perfecta.

## Conclusión Final
El módulo genérico **PCA9548A estaba defectuoso (quemado o en cortocircuito interno) desde fábrica**. En hardware de bajo costo, los fallos físicos de la placa/silicio ocurren frecuentemente.

### ⚠️ Regla de Oro sobre Voltajes
Aunque el PCA9548A tolera 5V en VCC, **nunca debe conectarse a 5V cuando se usa con el ESP32** (a menos que tenga su propio LDO/Level Shifter). Si se conecta a 5V, sus resistencias pull-up arrastrarán las líneas SDA y SCL a 5V, quemando inmediatamente los pines del ESP32 (cuyos GPIO son tolerantes estrictamente a 3.3V).
