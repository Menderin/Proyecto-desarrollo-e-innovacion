# Recomendaciones y Resumen del Proyecto

## Estado Actual
- Se descarta temporalmente el multiplexor PCA/TCA9548A.
- El proyecto vuelve al flujo original: sensores IR activan adquisicion magnetica y luego clasificador.
- Por ahora se usa un solo magnetometro HMC5883L conectado directo al bus I2C.

## Hardware Actual
- ESP32 estandar para desarrollo.
- Magnetometro HMC5883L directo al bus I2C en direccion `0x1E`.
- Bus I2C:
  - SDA: GPIO21
  - SCL: GPIO22
  - Frecuencia: 50 kHz
- Sensores IR:
  - IR 1: GPIO13
  - IR 2: GPIO12
- Logica de voltaje: 3.3 V.

## Conexiones Necesarias
| ESP32 | HMC5883L |
|---|---|
| 3V3 | VCC |
| GND | GND |
| GPIO21 | SDA |
| GPIO22 | SCL |

No conectar el magnetometro a 5 V si sus lineas SDA/SCL van directo al ESP32.

## Configuracion de Firmware
En `include/app_config.h`:

```c
#define I2C_MASTER_SDA  GPIO_NUM_21
#define I2C_MASTER_SCL  GPIO_NUM_22
#define I2C_FREQ_HZ     50000
#define APP_USE_I2C_MUX 0
#define APP_DIAG_MUX_ONLY 0
#define APP_CHARACTERIZATION_MODE 0
#define APP_DETECTOR_MODE 1
#define SENSOR_COUNT    1
#define MAGNETOMETER_I2C_ADDR 0x1E
```

## Descubrimientos
- El bus I2C del ESP32 funciona correctamente.
- El magnetometro directo fue detectado en `0x1E`.
- El multiplexor PCA9548A no respondio y arrastraba el bus, por lo que se considera defectuoso o inestable para esta etapa.
- El codigo ahora evita llamar al mux cuando `APP_USE_I2C_MUX` esta en `0`.

## Logs Esperados
Al iniciar, deberia aparecer:

```text
I2C inicializado correctamente
I2C config: SDA=GPIO21 SCL=GPIO22 freq=50000 Hz
Magnetometro directo encontrado en 0x1E - bus I2C OK
Magnetometro sensor 0 inicializado en 0x1e
Clasificador inicializado
```

Con `APP_CHARACTERIZATION_MODE 1`, el firmware no usa IR ni clasificador. Emite una linea JSON por segundo con la mediana de lecturas tomadas cada 20 ms:

```text
CHAR_JSON:{"seq":0,"ok":true,"window_ms":1000,"sample_ms":20,"samples":50,...}
```

Para volver al flujo del portico con IR y clasificador, cambiar `APP_CHARACTERIZATION_MODE` a `0`.

Con `APP_DETECTOR_MODE 1`, el firmware calibra baseline al arrancar y luego detecta continuamente:

```text
Calibrando baseline: 10 ventanas de 1000 ms sin objetos cerca
Baseline detector listo raw[x=... y=... z=...]
DETECT seq=0 estado=OK med[...] delta[...] mag2=... threshold2=...
DETECT seq=1 estado=ALERTA med[...] delta[...] mag2=... threshold2=...
```

El umbral inicial se genera en `include/detector_thresholds.h` con `tools/train_detector.py`.

## Captura de Datos
Usar el script:

```bash
python tools/capture_magnetometer.py --port COM4 --note "prueba cuchillos habitacion"
```

Controles durante la captura:
- `Enter`: iniciar una ronda de captura en la fase activa.
- `n + Enter`: avanzar a la siguiente fase sin capturar.
- `texto + Enter`: usar una fase personalizada, por ejemplo `cuchillo_12_5cm_horizontal`.
- `b + Enter`: capturar baseline estable. Por defecto toma 30 registros y usa la mediana por eje como referencia.
- `q + Enter`: terminar y guardar.

Cada ronda guarda por defecto 100 registros `CHAR_JSON`. Como el firmware emite 1 registro por segundo, una ronda dura aproximadamente 100 segundos. Para cambiarlo:

```bash
python tools/capture_magnetometer.py --port COM4 --records-per-round 30 --note "prueba rapida"
```

Para cambiar la cantidad de registros usados en el baseline:

```bash
python tools/capture_magnetometer.py --port COM4 --baseline-records 50 --note "baseline largo"
```

Los archivos quedan en:

```text
data/raw/magnetometer_characterization_YYYYMMDD_HHMMSS.json
```

## Siguientes Pasos
1. Compilar y flashear con la opcion 4 del menu.
2. Ejecutar `tools/capture_magnetometer.py` con el puerto correcto.
3. Presionar `b` para capturar baseline estable sin objetos cerca.
4. Presionar `Enter` para capturar la fase activa.
5. Capturar objetos a 5, 10, 15 y 20 cm.
6. Cuando llegue un multiplexor confiable, volver a `APP_USE_I2C_MUX 1` y subir `SENSOR_COUNT` a 2.

Fases predefinidas actuales:
- `cuchillo_pequeno_5cm`, `10cm`, `15cm`, `20cm`
- `cuchillo_grande_5cm`, `10cm`, `15cm`, `20cm`
- `cortacarton_5cm`, `10cm`, `15cm`, `20cm`
- `llaves_5cm`, `10cm`, `15cm`, `20cm`
- `celular_10cm`, `celular_20cm`
- `mochila_sin_objeto`

Las llaves, celular y mochila se incluyen como distractores para estimar falsos positivos.
