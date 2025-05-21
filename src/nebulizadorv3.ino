/*
  NEBULIZADOR V3
*/

// Include necessary libraries
#include <DHT.h>

// Pin definitions
const int LUMINOSITY_SENSOR_PIN = A0; // Pin for luminosity sensor
const int DHTPIN = 3;        // Pin for DHT11 sensor
const int SOLENOID_PIN = 8;  // Pin for solenoid valve
const int LED_STATUS_1 = 13; // Pin for LED indicator
const int LED_STATUS_2 = 12; // Pin for LED indicator
const int LED_STATUS_3 = 11; // Pin for LED indicator

// DHT sensor type
const int DHTTYPE = DHT11;
DHT dht(DHTPIN, DHTTYPE);

// Global variables
float humidity;
float temperature;
float hIndex;
int luminosity;

// Setup function
void setup()
{
  // Initialize serial communication
  Serial.begin(9600);

  Serial.println(F("Setup init."));

  // Configure pin modes
  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(LED_STATUS_1, OUTPUT);
  pinMode(LED_STATUS_2, OUTPUT);
  pinMode(LED_STATUS_3, OUTPUT);
  pinMode(LUMINOSITY_SENSOR_PIN, INPUT);

  // Initialize pins to LOW
  digitalWrite(SOLENOID_PIN, LOW);
  digitalWrite(LED_STATUS_1, LOW);
  digitalWrite(LED_STATUS_2, LOW);
  digitalWrite(LED_STATUS_3, LOW);

  // Initialize global variables to default values
  humidity = 0.0;
  temperature = 0.0;
  hIndex = 0.0;
  luminosity = 0;

  // Initialize the DHT sensor
  dht.begin();
  delay(50);

  // Light sequence to indicate setup is complete
  lightSequence();
}

// Loop function
void loop()
{
  // This file is intentionally left blank.
}

// Function to create a light sequence
void lightSequence()
{
  digitalWrite(LED_STATUS_1, HIGH);
  delay(200);
  digitalWrite(LED_STATUS_1, LOW);
  delay(200);
  digitalWrite(LED_STATUS_2, HIGH);
  delay(200);
  digitalWrite(LED_STATUS_2, LOW);
  delay(200);
  digitalWrite(LED_STATUS_3, HIGH);
  delay(200);
  digitalWrite(LED_STATUS_3, LOW);
  delay(200);
  digitalWrite(LED_STATUS_1, HIGH);
  digitalWrite(LED_STATUS_2, HIGH);
  digitalWrite(LED_STATUS_3, HIGH);
  delay(500);
  digitalWrite(LED_STATUS_1, LOW);
  digitalWrite(LED_STATUS_2, LOW);
  digitalWrite(LED_STATUS_3, LOW);
}

// Function to read DHT sensor data
void readDHTSensor()
{
  Serial.println(F("Iniciando lectura del sensor DHT..."));
  digitalWrite(LED_STATUS_2, HIGH); // Encender el segundo LED (LED_STATUS_2)
  digitalWrite(LED_STATUS_3, LOW);  // Asegurarse que el LED de error esté apagado al iniciar

  // Leer humedad
  humidity = dht.readHumidity();
  // Leer temperatura en Celsius (por defecto)
  temperature = dht.readTemperature();

  // Comprobar si alguna lectura falló
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println(F("¡Fallo al leer del sensor DHT!"));
    digitalWrite(LED_STATUS_2, LOW); // Apagar el segundo LED
    digitalWrite(LED_STATUS_3, HIGH); // Encender el tercer LED (LED de error)
    Serial.println(F("Lectura del sensor DHT finalizada con error."));
    return;
  }

  // Calcular el índice de calor en Celsius (isFahreheit = false)
  hIndex = dht.computeHeatIndex(temperature, humidity, false);

  // Imprimir los valores en el monitor serie
  Serial.print(F("Humedad: "));
  Serial.print(humidity);
  Serial.println(F(" %"));

  Serial.print(F("Temperatura: "));
  Serial.print(temperature);
  Serial.println(F(" °C"));

  Serial.print(F("Índice de calor: "));
  Serial.print(hIndex);
  Serial.println(F(" °C"));

  digitalWrite(LED_STATUS_2, LOW); // Apagar el segundo LED
  Serial.println(F("Lectura del sensor DHT finalizada correctamente."));
}