/*
  NEBULIZADOR V3
*/

// Include necessary libraries
#include <DHT.h>

// Pin definitions
const int LUMINOSITY_SENSOR_PIN = A0; // Pin for luminosity sensor
const int DHTPIN = 3;                 // Pin for DHT11 sensor
const int SOLENOID_PIN = 8;           // Pin for solenoid valve
const int LED_STATUS_1 = 13;          // Pin for LED indicator
const int LED_STATUS_2 = 12;          // Pin for LED indicator
const int LED_STATUS_3 = 11;          // Pin for LED indicator

// DHT sensor type
const int DHTTYPE = DHT11;
DHT dht(DHTPIN, DHTTYPE);

// Thresholds and constants
const int LUMINOSITY_ERROR_VALUE = -1;     // Constant for luminosity error
unsigned long VALVE_ACTIVE_TIME_MS = 5000; // 5 seconds default
const float MIN_HINDEX_THRESHOLD = 30.0;
const float MAX_HINDEX_THRESHOLD = 38.0;
const unsigned long MAX_FREQUENCY_MS = 300000; // Seconds maximum interval for proportional control, adjust as needed
unsigned long MIN_FREQUENCY_MS = 60000;        // Seconds minimum interval for proportional control, adjust as needed

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
  bootSequence();
}

// Loop function
void loop()
{
  // Read sensor data
  readDHTSensor();
  readLuminositySensor();

  // Check for sensor errors
  if (isnan(humidity) || isnan(temperature) || luminosity == LUMINOSITY_ERROR_VALUE)
  {
    Serial.println(F("Error reading sensors. Skipping valve control."));
    delay(5000); // Wait before retrying
    return;
  }

  Serial.print(F("Current hIndex: "));
  Serial.println(hIndex);

  if (hIndex >= MIN_HINDEX_THRESHOLD)
  {
    unsigned long delayTimeMs;

    if (hIndex >= MAX_HINDEX_THRESHOLD)
    {
      delayTimeMs = MAX_FREQUENCY_MS;
      Serial.print(F("hIndex above max threshold. Activating valve every "));
      Serial.print(delayTimeMs);
      Serial.println(F("ms."));
    }
    else
    {
      // Proportional control: map hIndex range to frequency range
      // Range of hIndex: MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD
      // Range of frequency: MAX_FREQUENCY_MS - MIN_FREQUENCY_MS (inverted: shorter delay for higher hIndex)
      // We want frequency to increase as hIndex increases, so delayTimeMs should decrease.
      // A simple linear mapping:
      // factor = (hIndex - MIN_HINDEX_THRESHOLD) / (MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD)
      // delayTimeMs = MAX_FREQUENCY_MS - factor * (MAX_FREQUENCY_MS - MIN_FREQUENCY_MS)
      float hIndexRange = MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD;
      if (hIndexRange <= 0)
        hIndexRange = 1; // Avoid division by zero if thresholds are equal
      float factor = (hIndex - MIN_HINDEX_THRESHOLD) / hIndexRange;
      delayTimeMs = MAX_FREQUENCY_MS - (unsigned long)(factor * (MAX_FREQUENCY_MS - MIN_FREQUENCY_MS));

      // Ensure delayTimeMs is within bounds
      if (delayTimeMs < MIN_FREQUENCY_MS)
        delayTimeMs = MIN_FREQUENCY_MS;
      if (delayTimeMs > MAX_FREQUENCY_MS)
        delayTimeMs = MAX_FREQUENCY_MS;

      Serial.print(F("hIndex in proportional range. Activating valve every "));
      Serial.print(delayTimeMs);
      Serial.println(F("ms."));
    }

    Serial.println(F("Activating solenoid valve."));
    controlSolenoidValve(true);
    digitalWrite(LED_STATUS_1, HIGH); // Indicate valve is active
    delay(VALVE_ACTIVE_TIME_MS);
    controlSolenoidValve(false);
    digitalWrite(LED_STATUS_1, LOW);
    Serial.println(F("Solenoid valve deactivated. Waiting for next cycle."));
    delay(delayTimeMs - VALVE_ACTIVE_TIME_MS); // Wait for the remaining part of the cycle
  }
  else
  {
    Serial.println(F("hIndex below minimum threshold. Valve remains closed."));
    // Optional: add a longer delay here if no action is needed for a while
    delay(MAX_FREQUENCY_MS); // Wait for a standard period before re-checking
  }
}

// Function to create a light sequence
void bootSequence()
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
  if (isnan(humidity) || isnan(temperature))
  {
    Serial.println(F("¡Fallo al leer del sensor DHT!"));
    digitalWrite(LED_STATUS_2, LOW);  // Apagar el segundo LED
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

// Function to read luminosity sensor data
void readLuminositySensor()
{
  Serial.println(F("Iniciando lectura del sensor de luminosidad..."));
  digitalWrite(LED_STATUS_2, HIGH); // Encender el segundo LED (LED_STATUS_2)
  digitalWrite(LED_STATUS_3, LOW);  // Asegurarse que el LED de error esté apagado al iniciar

  // Leer el valor del sensor de luminosidad
  luminosity = analogRead(LUMINOSITY_SENSOR_PIN);

  // Comprobar si la lectura es válida (ej. para un sensor de 10 bits, el rango es 0-1023)
  // Ajustar el rango según las características específicas del sensor y del ADC
  if (luminosity < 0 || luminosity > 1023)
  { // Asumiendo un ADC de 10 bits, valores fuera de 0-1023 son anómalos
    Serial.println(F("¡Error al leer el sensor de luminosidad! Valor fuera de rango."));
    digitalWrite(LED_STATUS_2, LOW);     // Apagar el LED de actividad
    digitalWrite(LED_STATUS_3, HIGH);    // Encender el LED de error
    luminosity = LUMINOSITY_ERROR_VALUE; // Indicar valor de error
    Serial.println(F("Lectura del sensor de luminosidad finalizada con error."));
    return;
  }

  // Imprimir el valor en el monitor serie
  Serial.print(F("Luminosidad: "));
  Serial.print(luminosity);

  digitalWrite(LED_STATUS_2, LOW); // Apagar el LED de actividad
  // LED_STATUS_3 permanece apagado si no hubo error
  Serial.println(F("Lectura del sensor de luminosidad finalizada correctamente."));
}

// Function to control the solenoid valve
// The solenoid valve is controlled by a MOSFET Trigger Switch Drive Module
// activate: true to open the valve, false to close the valve
void controlSolenoidValve(bool activate)
{
  if (activate)
  {
    digitalWrite(SOLENOID_PIN, HIGH); // Activate the MOSFET, opening the solenoid valve
    Serial.println(F("Válvula solenoide activada."));
  }
  else
  {
    digitalWrite(SOLENOID_PIN, LOW); // Deactivate the MOSFET, closing the solenoid valve
    Serial.println(F("Válvula solenoide desactivada."));
  }
}