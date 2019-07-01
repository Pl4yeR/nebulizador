# nebulizador v2

## Preparar

Librerias DHT
https://github.com/adafruit/Adafruit_Sensor
https://github.com/adafruit/DHT-sensor-library

Descomprimir DHT_lib.zip en la carpeta libraries de arduino.

## Componentes

- Arduino Uno
- Sensor de humedad/temperatura DHT 11.
- Sensor de luminosidad LDR
- Relé xx?

## Esquema

//TODO

## Cheatsheet

- A0: Sensor Luminosidad
- 11: Sensor humedad/temperatura
- 12: Control (in) relé (Activo en baja)
- 13: Led informativo (Activo en alta)

- 2: On/Off sensor luminosidad (Activo en alta)
- 3: On/Off sensor humedad/temperatura (Activo en alta)

- 4-9: Segundos en funcionamiento

## A tener en cuenta

- Si se desactiva el sensor de humedad/temperatura el sistema funcionará como un temporizador normal, activándose cada 4 minutos.
- Si se desactiva el sensor de luminosidad no se tendrá en cuenta este parámetro. El sistema funcionará como si siempre hubiese suficiente luz.
- El sensor de luminosidad ofrece valores entre 0 y 1023. Los valores son más altos mientras más luz haya.
- Cuando se habla de temperatura a la hora de elegir el modo de funcionamiento en realidad nos referimos a sensación térmica. La sensación térmica se calcula en base a la temperatura y la humedad relativa y no tiene por qué coincidir con la primera, pudiendo ser inferior o superior a ésta.

## Funcionamiento

Al conectar el sistema el LED informativo parpadeará 3 veces si todo ha ido correctamente.

### Casos de error

- Si la lectura de humedad/temperatura devuelve NaN (not a number) O la lectura de luminosidad devuelve un valor inferior a 0 o superior a 1024
    - El sistema entra en modo de error de sensores, no opera e intenta una nueva lectura en 90min.

### Casos especiales

- Si el sensor de humedad/temperatura está desactivado
    - El sistema funciona en modo temporizador, activándose cada 4 minutos
- Si el sensor de luminosidad está desactivado
    - El sistema funcionará como si estuviese a plena luz del día
- Si no hay luz y la temperatura está entre 36º y 38º
    - El sistema se activa y luego se pone en reposo durante 45min
- Si no hay luz y la temperatura es superior a 38º
    - El sistema se activa y luego se pone en reposo durante 35min
- Si no hay luz, la temperatura está entre 36º y 38º y la humedad relativa es alta
    - El sistema se activa y luego se pone en reposo durante 90min
- Si no hay luz, la temperatura es superior a 38º y la humedad relativa es alta
    - El sistema se activa y luego se pone en reposo durante 60min
- Si no hay luz y la temperatura no está en ninguno de los rangos descritos anteriormente
    - El sistema no opera y se pone en reposo durante 30min

### Casos estándar

Los casos estándar se dan cuando los sensores están activos, ofrecen lecturas no erróneas y la luminosidad es suficientemente alta. En estos casos el sistema actuará en base a las lecturas de humedad/temperatura.

- Temperatura inferior a 28º
    - El sistema no opera y se pone en reposo durante 30min
- Temperatura entre 28º y 30º
    - El sistema se activa y se pone en reposo durante 10min
- Temperatura entre 30º y 32º
    - El sistema se activa y se pone en reposo durante 4min
- Temperatura entre 32º y 34º
    - El sistema se activa y se pone en reposo durante 3min
- Temperatura entre 34º y 36º
    - El sistema se activa y se pone en reposo durante 75s
- Temperatura entre 36º y 38º
    - El sistema se activa y se pone en reposo durante 60s
- Temperatura entre 38º y 40º
    - El sistema se activa y se pone en reposo durante 40s
- Temperatura entre 40º y 42º
    - El sistema se activa y se pone en reposo durante 30s
- Temperatura superior a 42º
    - El sistema se activa y se pone en reposo durante 30s






