# Recomendaciones y Resumen del Proyecto

## 1️⃣ Objetivo Inicial
- Compilar y flashear el firmware para el *portal de clasificación magnética* basado en ESP32.
- Adaptar los headers C para compatibilidad con C++ (`extern "C"`).
- Configurar `sdkconfig.defaults` y pines de I2C.

## 2️⃣ Cambios Realizados
- **Pin SDA** reasignado a **GPIO 21** (antes GPIO 14).  
- **Pull‑up resistors** añadidos (4.7 kΩ) en SDA y SCL para garantizar niveles altos.
- `extern "C"` guards añadidos a todos los headers en `include/`.
- `sdkconfig.defaults` creado para fijar target ESP32 y parámetros de FreeRTOS.
- Se añadió un **probe I2C** al `app_main` para validar la presencia del magnetómetro (dirección `0x1E`).
- Se redujo la velocidad I2C a **50 kHz** (valor opcional para pruebas).

## 3️⃣ Problemas Encontrados
| Etapa | Síntoma | Causa probable |
|---|---|---|
| **Compilación** | Ninguno (éxito) | — |
| **Flasheo** | Éxito, firmware subido | — |
| **Ejecución** | `E … ACQ: Fallo lectura magnetometro sensor=… addr=0x1E: ESP_ERR_TIMEOUT` | Bus I²C sin pull‑ups o velocidad demasiado alta → señal no se recupera a nivel alto. |
| **Monitor** | No se muestra `I2C probe OK` | El probe también falla por las mismas razones del punto anterior. |

## 4️⃣ ¿Por qué se requieren resistencias pull‑up?
- El bus I²C usa **líneas open‑drain**; los dispositivos sólo pueden arrastrar la señal a GND.
- Cuando ninguno de los dispositivos está tirando a 0 V, la línea debe volver a **VCC** mediante una resistencia.
- Sin pull‑up la línea queda flotante → el maestro nunca reconoce el ACK del esclavo y se produce *timeout*.
- Valor típico **4.7 kΩ** (rango 2 kΩ‑10 kΩ) asegura transiciones rápidas y consumo razonable.

## 5️⃣ Recomendaciones de Acción
1. **Instalar Pull‑up resistors** de 4.7 kΩ en SDA (GPIO 21) y SCL (GPIO 15) a 3.3 V.
2. **Mantener la frecuencia I²C** en 100 kHz (o 50 kHz si el bus sigue inestable). Ajustar `I2C_FREQ_HZ` en `include/app_config.h` según sea necesario.
3. **Verificar alimentación** del magnetómetro (3.3 V) y conexión GND común.
4. **Ejecutar el escáner I²C** (código temporal en `app_main`) para confirmar que la dirección `0x1E` aparece en el log.
5. **Eliminar o comentar el `i2c_bus_probe`** una vez confirmada la correcta operación, o dejarlo como diagnóstico.
6. **Monitorear** con `idf.py -p /dev/ttyUSB0 monitor` y buscar la línea:
   ```
   I (xxxx) APP: I2C probe OK: magnetometer found at 0x1E
   ```
7. **Si sigue fallando**:
   - Re‑inspeccionar el cableado del multiplexor TCA9548A.
   - Probar con un único magnetómetro (sin multiplexor) para aislar posibles problemas de canal.
   - Asegurarse de que los pull‑ups estén conectados **antes** del multiplexor, de modo que todos los canales los compartan.

## 6️⃣ Próximos Pasos
- Implementar los cambios de hardware (pull‑ups).  
- Re‑compilar y flashear con la nueva configuración.  
- Verificar que el `i2c_bus_probe` reporte **OK** y que los logs de adquisición ya no muestren `ESP_ERR_TIMEOUT`.  
- Cuando el bus funcione, sustituir el *stub* de Edge‑Impulse por el modelo ML real.

---
*Este documento reúne todo lo discutido y las acciones recomendadas para lograr un funcionamiento estable del proyecto.*
