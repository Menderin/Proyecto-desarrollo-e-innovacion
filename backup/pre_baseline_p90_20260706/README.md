# Respaldo previo a baseline + p90

Estado funcional anterior al cambio del 6 de julio de 2026.

- I2C: SDA GPIO26, SCL GPIO25, 50 kHz.
- Magnetometro HMC5883L directo en 0x1E.
- El clasificador usaba la primera muestra de cada cruce como referencia.
- La caracteristica era la maxima magnitud observada durante el cruce.
- El umbral entrenado de 93 se limitaba a un minimo efectivo de 180.

Para volver al comportamiento anterior, reemplazar:

- `src/edge_impulse_adapter.cpp` por `edge_impulse_adapter.cpp.txt`
- `include/app_config.h` por `app_config.h.txt`

