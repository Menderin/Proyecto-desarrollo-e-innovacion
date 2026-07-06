# Nodo ESP32-CAM - fase 1

Prototipo aislado para validar la camara y la logica de una fotografia
asociada a cada cruce. Todavia no modifica el firmware del ESP32 detector y
no contiene Wi-Fi, Telegram ni credenciales.

La prueba anterior que capturaba una imagen por segundo se conserva en
`backup/camera_standalone_test_main.c.txt`.

## Cableado UART propuesto

No se usa UART0, porque queda reservada para flashear y abrir el monitor.

| ESP32 detector | ESP32-CAM |
|---|---|
| GPIO17 TX | GPIO13 RX |
| GPIO16 RX | GPIO14 TX |
| GND | GND |

Ambas UART trabajan a 115200 baud y logica de 3.3 V.

La microSD no se puede utilizar porque GPIO13 y GPIO14 quedan dedicados al
enlace con el detector.

## Protocolo inicial

Cada comando termina en salto de linea (`\n`).

```text
PING
START,42,IR1_TO_IR2
SAFE,42
ALERT,42,347,180
```

- `PING`: responde `READY`.
- `START`: toma una foto JPEG QVGA y la copia a PSRAM.
- `SAFE`: descarta la foto del cruce indicado.
- `ALERT`: conserva la foto y registra `PHOTO_READY_FOR_TELEGRAM`.

Telegram se agregara despues en el comentario marcado dentro de
`main/detector_link.c`. Un nuevo `START` libera cualquier foto pendiente para
evitar fugas de memoria.

## Compilar

Desde este directorio, con el entorno ESP-IDF cargado:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM4 flash monitor
```

La configuracion por defecto habilita PSRAM, flash de 4 MB y una pila principal
de 8192 bytes.

## Alimentacion

Usar 5 V estables para la ESP32-CAM y compartir GND con el detector. No
alimentar la placa desde la salida 3.3 V de un adaptador serial debil.

## Pendiente para la fase 2

- Emisor UART en el firmware detector.
- Tarea independiente para captura si se necesita una demora de encuadre.
- Conexion Wi-Fi.
- Envio HTTPS `sendPhoto` a Telegram.
- Reintentos y descarte temporizado de fotografias.
