/*
  NEBULIZADOR POLLUNO V2
*/

#include <DHT.h>

// Puertos de sensores
#define senLum A0
#define senHumTemp 11
#define lumEnabled 2
#define tempEnabled 3

#define l4 4
#define l5 5
#define l6 6
#define l7 7
#define l8 8
#define l9 9
#define rele 12
#define led 13

//DHT
#define DHTTYPE DHT11
DHT dht(senHumTemp, DHTTYPE);


// Valores limites
#define T25 25
#define T28 28
#define T30 30
#define T32 32
#define T34 34
#define T36 36
#define T38 38
#define T40 40
#define T42 42
#define LUMDIA 650
#define ERRORTEMP -275
#define NOTEMP -276
#define ERRORLUM -1
#define NOLUM -2
#define HUM35 35
#define HUM40 40
#define HUM50 50
#define HUM70 70
#define HUM85 85
#define CALORHUMRELALTA 1000
#define CALOR2HUMRELALTA 2000

// Valores sensores
float humedad;
float temperatura;
float hIndex;
int luminosidad;

// Auxiliares
long ultimaEjecucion;

// Se lanza al iniciar por primera vez
void setup() {
  // Inicializamos la consola serial
  Serial.begin(9600);

  Serial.println(F("Setup init."));

  // Inicializamos los posibles modos
  pinMode(led, OUTPUT);
  pinMode(rele, OUTPUT);
  pinMode(lumEnabled, INPUT);
  pinMode(tempEnabled, INPUT);

  pinMode(l4, INPUT);
  pinMode(l5, INPUT);
  pinMode(l6, INPUT);
  pinMode(l7, INPUT);
  pinMode(l8, INPUT);
  pinMode(l9, INPUT);

  // Reseteamos todo
  off();
  humedad = temperatura = hIndex  = 0;
  luminosidad = 0;

  // Inicializamos el sensor temp/hum
  dht.begin();
  delay(2000);

  avisoReinicio();

  ultimaEjecucion = millis();

  Serial.println(F("Setup end."));
}

void avisoReinicio()
{
  // Hace parpadear los leds 3 veces para avisar de un reinicio
  for (int i = 0; i < 3; i++)
  {
    on(led);
    delay(500);
    off(led);
    delay(500);
  }
}

// MAIN
void loop()
{
  leerValores(); // Leemos los valores de los sensores
  ejecutaCaso(calculaCaso());
}

float calculaFeelTemp() {
  float feelTemp = ERRORTEMP;
  if (humedad == ERRORTEMP) {
    feelTemp = ERRORTEMP;
  } else if (humedad == NOTEMP) {
    feelTemp = NOTEMP;
  } else if (humedad < HUM50) {
    feelTemp = hIndex;
  } else if (humedad < HUM70) {
    if (temperatura <= T32) {
      feelTemp = temperatura;
    } else if (temperatura <= T38) {
      feelTemp = temperatura - 2;
    } else {
      feelTemp = temperatura - 3;
    }
  } else if (humedad < HUM85) {
    if (temperatura <= T30) {
      feelTemp = temperatura;
    } else if (temperatura <= T34) {
      feelTemp = CALORHUMRELALTA;
    } else {
      feelTemp = CALOR2HUMRELALTA;
    }
  } else {
    if (temperatura <= T30) {
      feelTemp = temperatura;
    } else if (temperatura <= T32) {
      feelTemp = CALORHUMRELALTA;
    } else {
      feelTemp = CALOR2HUMRELALTA;
    }
  }

  return feelTemp;
}

int calculaCaso() {
  int caso = -1;

  // Calculamos la sensación térmica
  float feelTemp = calculaFeelTemp();

  Serial.print(F("Sensacion calculada: "));
  Serial.println(feelTemp);

  if (luminosidad == ERRORLUM) {
    Serial.println(F("Error de sensor de luminosidad"));
    caso  = -1;
  } else if (luminosidad != NOLUM && luminosidad < LUMDIA && feelTemp != NOTEMP && feelTemp > T36 && feelTemp <= T38 && feelTemp != CALORHUMRELALTA && feelTemp != CALOR2HUMRELALTA) {
    Serial.println(F("No hay luz, pero temperatura muy elevada (36 - 38 ºC)"));
    caso = 13;
  } else if (luminosidad != NOLUM && luminosidad < LUMDIA && feelTemp != NOTEMP && feelTemp > T38 && feelTemp != CALORHUMRELALTA && feelTemp != CALOR2HUMRELALTA && feelTemp != NOTEMP) {
    Serial.println(F("No hay luz, pero temperatura muy elevada (> 38 ºC)"));
    caso = 14;
  } else if (luminosidad != NOLUM && luminosidad < LUMDIA && feelTemp != NOTEMP && feelTemp == CALORHUMRELALTA) {
    Serial.println(F("No hay luz, hum relativa alta y calor"));
    caso = 15;
  } else if (luminosidad != NOLUM && luminosidad < LUMDIA && feelTemp != NOTEMP && feelTemp == CALOR2HUMRELALTA) {
    Serial.println(F("No hay luz, hum relativa alta y mucha calor)"));
    caso = 16;
  } else if (luminosidad != NOLUM && luminosidad < LUMDIA) {
    Serial.println(F("No hay luz suficiente"));
    caso = 0;
  } else if (feelTemp == NOTEMP) {
    caso = 17;
  } else if (feelTemp >= 0 && feelTemp <= T25) {
    caso = 1;
  } else if (feelTemp > T25 && feelTemp <= T28) {
    caso = 2;
  } else  if (feelTemp > T28 && feelTemp <= T30) {
    caso = 3;
  } else  if (feelTemp > T30 && feelTemp <= T32) {
    caso = 4;
  } else if (feelTemp > T32 && feelTemp <= T34) {
    caso = 5;
  } else if (feelTemp > T34 && feelTemp <= T36) {
    caso = 6;
  } else  if (feelTemp > T36 && feelTemp <= T38) {
    caso = 7;
  } else  if (feelTemp > T38 && feelTemp <= T40) {
    caso = 8;
  } else  if (feelTemp > T40 && feelTemp <= T42) {
    caso = 9;
  } else  if (feelTemp > T42) {
    caso = 10;
  } else if (feelTemp == CALORHUMRELALTA) {
    Serial.println(F("Hum relativa alta y calor"));
    caso = 11;
  } else if (feelTemp == CALOR2HUMRELALTA) {
    Serial.println(F("Hum relativa alta y mucha calor"));
    caso = 12;
  } else  if (feelTemp < 0 && feelTemp != ERRORTEMP) {
    Serial.println(F("Temp negativa ¿¿??"));
    caso = -2;
  } else {
    Serial.println(F("Error de sensor de temp"));
    caso  = -1;
  }

  return caso;
}

void ejecutaCaso(int caso) {
  switch (caso) {
    case 0:
      // No luz
      Serial.println(F("Caso 0: No hay luz: 30min"));
      delay(1800000);
      break;
    case 1:
    // 0 - 25
    case 2:
      // 25 - 28
      Serial.println(F("Caso 1-2: Temp baja (<=28): 30min"));
      delay(1800000);
      break;
    case 3:
      // 28 - 30
      ejecutaModo();
      Serial.println(F("Caso 3: 10min"));
      delay(600000);
      break;
    case 4:
      // 30 - 32
      ejecutaModo();
      Serial.println(F("Caso 4: 4min"));
      delay(240000);
      break;
    case 5:
      // 32 - 34
      ejecutaModo();
      Serial.println(F("Caso 5: 3min"));
      delay(180000);
      break;
    case 6:
      // 34 - 36
      ejecutaModo();
      Serial.println(F("Caso 6: 1min"));
      delay(60000);
      break;
    case 7:
      // 36 - 38
      ejecutaModo();
      Serial.println(F("Caso 7: 50s"));
      delay(50000);
      break;
    case 8:
      // 38 - 40
      ejecutaModo();
      Serial.println(F("Caso 8: 40s"));
      delay(40000);
      break;
    case 9:
      // 40 - 42
      ejecutaModo();
      Serial.println(F("Caso 9: 30s"));
      delay(30000);
      break;
    case 10:
      // 42 - inf
      ejecutaModo();
      Serial.println(F("Caso 10: 30s"));
      delay(30000);
      break;
    case 11:
      // %hum rel alta y calor
      ejecutaModo();
      Serial.println(F("Caso 11: 15min"));
      delay(900000);
      break;
    case 12:
      // %hum rel alta y mucha calor
      ejecutaModo();
      Serial.println(F("Caso 12: 10min"));
      delay(600000);
      break;
    case 13:
      // 36 - 38 (no luz)
      ejecutaModo();
      Serial.println(F("Caso 13: Calor sin luz (36-38): 45min"));
      delay(2700000);
      break;
    case 14:
      // 38 - inf (no luz)
      ejecutaModo();
      Serial.println(F("Caso 14: Calor sin luz (> 38): 35min"));
      delay(2100000);
      break;
    case 15:
      // Calor %hum alta (no luz)
      ejecutaModo();
      Serial.println(F("Caso 15: Calor sin luz y humedad alta: 1.5h"));
      delay(5400000);
      break;
    case 16:
      // Mucha calor %hum alta (no luz)
      ejecutaModo();
      Serial.println(F("Caso 16: Mucha calor sin luz y humedad alta: 1h"));
      delay(3600000);
      break;
    case 17:
      // Sensor de temperatura/humedad desactivado. Modo temporizador.
      ejecutaModo();
      Serial.println(F("Caso 17: Sensor de temperatura/humedad desactivado. Modo temporizador: 4min"));
      delay(240000);
      break;
    case -1:
      Serial.println(F("Error: REVISE SENSORES: 90min"));
      delay(5400000);
      break;
    case -2:
      Serial.println(F("Temp negativa: 60min"));
      delay(3600000);
      break;
    default:
      Serial.println(F("El sistema no esta funcionando correctamente: 30min"));
      delay(1200000);
      break;
  }
}

void ejecutaModo() {

  off();

  long tiempoms = millis() - ultimaEjecucion;
  Serial.print(F("Ultima ejecucion (ms): "));
  Serial.println(tiempoms);

  int msDelay = 4 * digitalRead(l4)
                + 5 * digitalRead(l5)
                + 6 * digitalRead(l6)
                + 7 * digitalRead(l7)
                + 8 * digitalRead(l8)
                + 9 * digitalRead(l9);
  msDelay *= 1000;

  if (msDelay > 0) {
    Serial.print(F("Tiempo activo: "));
    Serial.print(msDelay);
    Serial.println(F(" ms."));

    on();
    delay(msDelay);
    off();

    ultimaEjecucion = millis();
  } else {
    Serial.println(F("Ningun modo seleccionado. El sistema no se activara."));
  }
}

void leerValores()
{
  Serial.println(F("Leyendo informacion de los sensores..."));
  leeLuminosidad();
  leeHumTemp();
}

void leeHumTemp()
{
  if (digitalRead(tempEnabled) == 0) {
    temperatura = humedad = hIndex = NOTEMP;
    Serial.println(F("Sensor de temperatura y humedad desactivado."));
    return;
  }
  //Lectura de los sensores de humedad y temperatura
  humedad = dht.readHumidity();
  temperatura = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(humedad) || isnan(temperatura)) {
    Serial.println("Failed to read from DHT sensor!");
    temperatura = humedad = hIndex = ERRORTEMP;
    return;
  }

  hIndex = dht.computeHeatIndex(temperatura, humedad, false);

  Serial.print(F("Humedad: "));
  Serial.print(humedad);
  Serial.println(F("(%)."));
  Serial.print(F("Temperatura: "));
  Serial.print(temperatura);
  Serial.println(F("(C)."));
  Serial.print(F("Heat index: "));
  Serial.print(hIndex);
  Serial.println(F("(C)."));

  delay(2000);
}

void leeLuminosidad()
{
  if (digitalRead(lumEnabled) == 0) {
    luminosidad = NOLUM;
    Serial.println(F("Sensor de luminosidad desactivado."));
    return;
  }
  // Lectura del sensor de luminosidad
  luminosidad = analogRead(senLum);
  if (luminosidad <= 1 || luminosidad > 1024) {
    luminosidad = ERRORLUM;
  }
  Serial.print(F("Luminosidad: "));
  Serial.println(luminosidad);
}

void on() {
  digitalWrite(led, HIGH);
  digitalWrite(rele, LOW);
}

void off() {
  digitalWrite(rele, HIGH);
  digitalWrite(led, LOW);
}

void off(int l)
{
  digitalWrite(l, LOW);
}

void on(int l)
{
  digitalWrite(l, HIGH);
}

