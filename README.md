# Proyecto-desarrollo-e-innovacion

## Calibracion ambiental y registro de cruces

El firmware normal calibra automaticamente el ambiente durante los primeros
2 segundos. El portico debe estar vacio hasta que el monitor muestre
`Calibracion lista`.

El tornillo y otras piezas metalicas fijas quedan incluidas en esa baseline.
No se deben mover despues de calibrar.

### Registrar cruces reales

No abrir `idf.py monitor` al mismo tiempo: solo un programa puede usar el
puerto serial. Para registrar varios objetos en una sola sesion:

```powershell
python -m pip install pyserial
python tools/capture_crossings.py --port COM4 --count 10
```

El programa pedira clase, nombre y cantidad para cada objeto. Al terminar una
serie permite iniciar la siguiente sin cerrar el puerto ni reiniciar la
baseline. Escribir `fin` cuando no queden objetos.

Despues de iniciar el capturador, pulsar RESET en el ESP32 sin objetos cerca.
Esperar `Baseline recibida` antes de medir.

Tambien se conserva el modo de una sola etiqueta:

```powershell
python tools/capture_crossings.py --port COM4 --label llaves --class allowed --count 10
```

El script guarda cada cruce inmediatamente en `data/crossings/`. Si se
interrumpe con `Ctrl+C`, las mediciones ya realizadas no se pierden.

Conviene realizar todos los cruces con la misma trayectoria y velocidad que
tendra el portico durante la demostracion.

### Entrenar el umbral p90

Despues de capturar objetos peligrosos y permitidos:

```powershell
python tools/train_crossings.py data/crossings/*.json
```

El entrenador actualiza `include/detector_thresholds.h` y genera
`data/crossing_training_report.json`. Luego se debe recompilar y flashear.

El respaldo del clasificador anterior esta en
`backup/pre_baseline_p90_20260706/`.
