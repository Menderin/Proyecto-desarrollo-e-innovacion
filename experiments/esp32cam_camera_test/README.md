# Nodo ESP32-CAM

Nodo separado que captura una fotografia al recibir `START`, la descarta con
`SAFE` o la envia a un grupo de Telegram con `ALERT`.

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
- `ALERT`: transfiere la foto a una cola y la envia a Telegram.

El envio se ejecuta en una tarea independiente. Una conexion lenta no bloquea
la UART. Hay reintentos, sincronizacion SNTP para TLS y liberacion de la foto
en PSRAM al terminar.

## Crear y configurar el bot

1. Crear el bot con `@BotFather` y guardar el token.
2. Agregar el bot al grupo de Telegram.
3. Escribir un mensaje cualquiera en el grupo.
4. Consultar `https://api.telegram.org/bot<TOKEN>/getUpdates`.
5. Buscar `chat.id`; en grupos normalmente es un numero negativo.

No guardar el token en archivos versionados ni pegarlo en capturas publicas.

Configurar desde este directorio:

```powershell
idf.py menuconfig
```

Entrar en `Nodo ESP32-CAM` y completar:

- `Habilitar envio de alertas a Telegram`
- Nombre de la red Wi-Fi
- Contrasena Wi-Fi
- Token del bot
- Chat ID del grupo
- Opcional para diagnostico: `Enviar foto de prueba a Telegram al arrancar`

`sdkconfig` esta ignorado por Git para evitar publicar estas credenciales.

## Probar Telegram sin el detector

Para confirmar que la camara, Wi-Fi, TLS y Telegram funcionan sin depender de
los infrarrojos ni de la otra ESP32:

1. Abrir `menuconfig`.
2. Entrar en `Nodo ESP32-CAM`.
3. Activar `Enviar foto de prueba a Telegram al arrancar`.
4. Compilar y flashear la ESP32-CAM.
5. Reiniciar la placa con GPIO0 libre.

En el monitor deberian aparecer mensajes parecidos a:

```text
Prueba Telegram activa: se enviara una foto al arrancar
PHOTO_CAPTURED id=9999 bytes=...
Prueba Telegram: foto encolada, espera envio al grupo
Foto enviada a Telegram id=9999 bytes=...
```

Despues de probar, conviene desactivar esa opcion para que no envie una foto
cada vez que arranca la maqueta.

## Compilar

Desde este directorio, con el entorno ESP-IDF cargado:

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM4 flash monitor
```

La configuracion por defecto habilita PSRAM, flash de 4 MB y una pila principal
de 8192 bytes. Telegram utiliza una particion de aplicacion de 3 MB porque TLS
y el bundle de certificados superan el tamano de la particion estandar.

## Alimentacion

Usar 5 V estables para la ESP32-CAM y compartir GND con el detector. No
alimentar la placa desde la salida 3.3 V de un adaptador serial debil.

## Flujo de ejecucion

```text
START → captura JPEG QVGA en PSRAM
SAFE  → libera la foto
ALERT → encola foto → espera Wi-Fi → sincroniza hora → HTTPS sendPhoto
```

La descripcion enviada incluye identificador del cruce, direccion, p90 y
umbral. La Bot API recibe el JPEG mediante `multipart/form-data`.

## Integracion con detector

El firmware del detector emite por UART los comandos `START/SAFE/ALERT`.
Para diagnosticar la comunicacion, el detector tambien imprime respuestas de
la ESP32-CAM como `READY`, `PHOTO_OK`, `PHOTO_FAIL`, `ALERT_QUEUED` o
`ALERT_NO_PHOTO`.
