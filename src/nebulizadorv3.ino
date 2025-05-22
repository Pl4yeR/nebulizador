/*
  NEBULIZADOR V3
*/

// Include necessary libraries
#include <DHT.h>
#include "Arduino.h"

// Pin definitions
const int LUMINOSITY_SENSOR_PIN = A0; // Pin for luminosity sensor
const int BUTTON_PIN = 2;             // Pin for the button
const int DHTPIN = 3;                 // Pin for DHT11 sensor
const int SOLENOID_PIN = 8;           // Pin for solenoid valve
const int LED_STATUS_1 = 13;          // Pin for LED indicator
const int LED_STATUS_2 = 12;          // Pin for LED indicator
const int LED_STATUS_3 = 11;          // Pin for LED indicator

// DHT sensor type
const int DHTTYPE = DHT11;
DHT dht(DHTPIN, DHTTYPE);

// Thresholds and constants
const unsigned int LOOP_DELAY_MS = 250;        // Delay for the main loop
const unsigned int LONG_PRESS_DURATION = 2000; // Duration for long press in milliseconds

const int LUMINOSITY_THRESHOLD = 640; // Threshold for luminosity sensor

const float MIN_HINDEX_THRESHOLD = 32.0;
const float MAX_HINDEX_THRESHOLD = 39.0;
const unsigned long MAX_FREQUENCY_MS = 600000; // Seconds maximum interval for proportional control, adjust as needed
const unsigned long MIN_FREQUENCY_MS = 90000;  // Seconds minimum interval for proportional control, adjust as needed

const unsigned int LED1_BLINK_INTERVAL_MS = 5000; // Blink every X seconds
const unsigned int LED1_BLINK_DURATION_MS = 100;  // Duration of each blink

// Global variables
float humidity = 0.0;
float temperature = 0.0;
float hIndex = 0.0;
int luminosity = 0.0;
unsigned long valveActiveTimeMS = 5000; // 5 seconds default
unsigned long lastButtonPressTime = 0;  // For button debouncing
int lastButtonState = HIGH;             // Assuming pull-up resistor, so HIGH when not pressed

// Timing variables for non-blocking operation
unsigned long cycleStartTime = 0;
unsigned long currentCycleDelayMs = 0; // Stores the calculated delay for the current cycle
unsigned long sensorReadTime = 0;      // Time of the last sensor read
bool isValveActive = false;

// Variables for LED_STATUS_1 blinking
int led1BlinksToDo = valveActiveTimeMS / 5000;
bool led1BlinkState = LOW;
int led1BlinkCount = 0;
unsigned long led1LastToggleTime = 0;

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
  pinMode(BUTTON_PIN, INPUT_PULLUP); // Initialize button pin with internal pull-up

  // Initialize pins to LOW
  digitalWrite(SOLENOID_PIN, LOW);
  digitalWrite(LED_STATUS_1, LOW);
  digitalWrite(LED_STATUS_2, LOW);
  digitalWrite(LED_STATUS_3, LOW);

  // Initialize the DHT sensor
  dht.begin();
  delay(50);

  // Light sequence to indicate setup is complete
  bootSequence();
}

// Loop function
void loop()
{
  unsigned long currentTime = millis();

  blinkLedStatusLoop(currentTime);

  readSensorsLoop(currentTime);

  readButton();

  manageValveLoop(currentTime);

  delay(LOOP_DELAY_MS);
}

// Function to create a light sequence
void bootSequence()
{
  digitalWrite(LED_STATUS_1, HIGH);
  delay(500);
  digitalWrite(LED_STATUS_1, LOW);
  delay(500);
  digitalWrite(LED_STATUS_2, HIGH);
  delay(500);
  digitalWrite(LED_STATUS_2, LOW);
  delay(500);
  digitalWrite(LED_STATUS_3, HIGH);
  delay(500);
  digitalWrite(LED_STATUS_3, LOW);
  delay(500);
  digitalWrite(LED_STATUS_1, HIGH);
  digitalWrite(LED_STATUS_2, HIGH);
  digitalWrite(LED_STATUS_3, HIGH);
  delay(2000);
  digitalWrite(LED_STATUS_1, LOW);
  digitalWrite(LED_STATUS_2, LOW);
  digitalWrite(LED_STATUS_3, LOW);
}

/**
 * Function to handle LED_STATUS_1 blinking
 * This function checks if the time since the last toggle is greater than LED1_BLINK_INTERVAL_MS
 * If so, it toggles the LED_STATUS_1 state and updates the blink count
 * The LED blinks a number of times based on the valveActiveTimeMS
 */
void blinkLedStatusLoop(unsigned long currentTime)
{
  // Handle LED_STATUS_1 blinking trigger
  if (currentTime - led1LastToggleTime >= LED1_BLINK_INTERVAL_MS && led1BlinkCount >= led1BlinksToDo) // Only start new sequence if old one is done
  {
    led1BlinkCount = 0;               // Reset blink counter
    led1BlinkState = LOW;             // Ensure LED starts off for the first blink
    led1LastToggleTime = currentTime; // Initialize toggle time
    digitalWrite(LED_STATUS_1, LOW);  // Ensure LED is off before starting a new blink sequence if it was left on
  }

  // Non-blocking LED_STATUS_1 blinking execution
  if (led1BlinkCount < led1BlinksToDo)
  {
    if (currentTime - led1LastToggleTime >= LED1_BLINK_DURATION_MS)
    {
      led1LastToggleTime = currentTime;
      led1BlinkState = !led1BlinkState; // Toggle LED state
      digitalWrite(LED_STATUS_1, led1BlinkState);

      if (led1BlinkState == LOW)
      { // Count a full blink when LED turns OFF
        led1BlinkCount++;
      }
    }
  }
}

/**
 * Function to read sensors in a non-blocking way
 * This function checks if the time since the last sensor read is greater than currentCycleDelayMs
 * If so, it reads the DHT and luminosity sensors
 */
void readSensorsLoop(unsigned long currentTime)
{
  if (currentTime - sensorReadTime >= currentCycleDelayMs)
  { // Read sensors every currentCycleDelayMs ms
    sensorReadTime = currentTime;
    Serial.println(F("Iniciando lectura de los sensores..."));
    readDHTSensor();
    readLuminositySensor();
  }
}

/**
 * Function to manage the solenoid valve control loop
 * This function checks the humidity index (hIndex) and controls the solenoid valve accordingly
 * It uses a proportional control mechanism to adjust the frequency of valve activation based on hIndex
 */
void manageValveLoop(unsigned long currentTime)
{
  // Check for sensor errors
  if (isnan(humidity) || isnan(temperature) || isnan(hIndex) || isnan(luminosity))
  {
    if (isValveActive)
    { // If valve was active (e.g. hIndex just dropped or luminosity dropped), deactivate it
      Serial.println(F("Error reading sensors. Deactivating solenoid valve."));
      controlSolenoidValve(false);
    }
    // Check if it's time to print the status message
    if (currentTime - cycleStartTime >= currentCycleDelayMs)
    {
      Serial.println(F("Error reading sensors. Skipping valve control."));
    }

    return;
  }

  if (luminosity >= LUMINOSITY_THRESHOLD && hIndex >= MIN_HINDEX_THRESHOLD)
  {
    if (!isValveActive && (currentTime - cycleStartTime >= currentCycleDelayMs))
    {
      // Time to start a new cycle and activate the valve

      // Proportional control: map hIndex range to frequency range
      // Range of hIndex: MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD
      // Range of frequency: MAX_FREQUENCY_MS - MIN_FREQUENCY_MS (inverted: shorter delay for higher hIndex)
      // We want frequency to increase as hIndex increases, so currentCycleDelayMs should decrease.
      // A simple linear mapping:
      // factor = (hIndex - MIN_HINDEX_THRESHOLD) / (MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD)
      // currentCycleDelayMs = MAX_FREQUENCY_MS - factor * (MAX_FREQUENCY_MS - MIN_FREQUENCY_MS)
      float hIndexRange = MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD;
      if (hIndexRange <= 0)
        hIndexRange = 1;

      float factor = (hIndex - MIN_HINDEX_THRESHOLD) / hIndexRange;

      currentCycleDelayMs = MAX_FREQUENCY_MS - (unsigned long)(factor * (MAX_FREQUENCY_MS - MIN_FREQUENCY_MS));
      if (currentCycleDelayMs < MIN_FREQUENCY_MS)
        currentCycleDelayMs = MIN_FREQUENCY_MS;
      if (currentCycleDelayMs > MAX_FREQUENCY_MS)
        currentCycleDelayMs = MAX_FREQUENCY_MS;

      Serial.print(F("hIndex ABOVE threshold. Activating valve for "));
      Serial.print(valveActiveTimeMS);
      Serial.println(F("ms."));

      controlSolenoidValve(true);
      cycleStartTime = currentTime;
    }

    if (isValveActive && (currentTime - cycleStartTime >= valveActiveTimeMS))
    {
      // Time to deactivate the valve
      controlSolenoidValve(false);
    }
  }
  else // hIndex < MIN_HINDEX_THRESHOLD OR luminosity < LUMINOSITY_THRESHOLD
  {
    if (isValveActive)
    { // If valve was active (e.g. hIndex just dropped or luminosity dropped), deactivate it
      Serial.println(F("hIndex or luminosity dropped BELOW threshold. Deactivating solenoid valve."));
      controlSolenoidValve(false);
    }
    // Check if it's time to print the status message
    if (currentTime - cycleStartTime >= currentCycleDelayMs)
    {
      Serial.println(F("hIndex or luminosity BELOW threshold. Valve remains closed."));
      cycleStartTime = currentTime;
      currentCycleDelayMs = MAX_FREQUENCY_MS;
    }
  }
}

// Function to read DHT sensor data
void readDHTSensor()
{
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
    digitalWrite(LED_STATUS_2, LOW);  // Apagar el LED de actividad
    digitalWrite(LED_STATUS_3, HIGH); // Encender el tercer LED (LED de error)
    hIndex = NAN;                     // Indicar valor de error
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

  digitalWrite(LED_STATUS_2, LOW); // Apagar el LED de actividad
}

// Function to read luminosity sensor data
void readLuminositySensor()
{
  digitalWrite(LED_STATUS_2, HIGH); // Encender el segundo LED (LED_STATUS_2)
  digitalWrite(LED_STATUS_3, LOW);  // Asegurarse que el LED de error esté apagado al iniciar

  // Leer el valor del sensor de luminosidad
  luminosity = analogRead(LUMINOSITY_SENSOR_PIN);

  // Comprobar si la lectura es válida (ej. para un sensor de 10 bits, el rango es 0-1023)
  // Ajustar el rango según las características específicas del sensor y del ADC
  if (luminosity < 0 || luminosity > 1023)
  { // Asumiendo un ADC de 10 bits, valores fuera de 0-1023 son anómalos
    Serial.println(F("¡Error al leer el sensor de luminosidad!"));
    digitalWrite(LED_STATUS_2, LOW);  // Apagar el LED de actividad
    digitalWrite(LED_STATUS_3, HIGH); // Encender el LED de error
    luminosity = NAN;                 // Indicar valor de error
    return;
  }

  // Imprimir el valor en el monitor serie
  Serial.print(F("Luminosidad: "));
  Serial.println(luminosity);

  digitalWrite(LED_STATUS_2, LOW); // Apagar el LED de actividad
}

// Function to control the solenoid valve
// The solenoid valve is controlled by a MOSFET Trigger Switch Drive Module
// activate: true to open the valve, false to close the valve
void controlSolenoidValve(bool activate)
{
  isValveActive = activate;

  if (activate)
  {
    digitalWrite(SOLENOID_PIN, HIGH); // Activate the MOSFET, opening the solenoid valve
    Serial.println(F("Válvula solenoide activada."));
    digitalWrite(LED_STATUS_2, HIGH); // Turn on the activity LED
  }
  else
  {
    digitalWrite(SOLENOID_PIN, LOW); // Deactivate the MOSFET, closing the solenoid valve
    Serial.println(F("Válvula solenoide desactivada."));
    digitalWrite(LED_STATUS_2, LOW); // Turn off the activity LED
  }
}

void readButton()
{
  unsigned long msFromPress = millis() - lastButtonPressTime;

  if (msFromPress < 50)
  {
    // Debounce the dirty way
    return;
  }

  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW)
  {
    Serial.print(F("Manual btn press: "));
    lastButtonPressTime = millis();
  }
  else if (lastButtonState == LOW && currentButtonState == HIGH)
  {
    if (msFromPress < LONG_PRESS_DURATION)
    {
      Serial.println(F("Button short press"));
      valveActiveTimeMS += 5000;
      if (valveActiveTimeMS > 20000)
      {
        valveActiveTimeMS = 5000;
      }
      Serial.print(F("New valveActiveTimeMS: "));
      Serial.println(valveActiveTimeMS);

      led1BlinksToDo = valveActiveTimeMS / 5000; // Update the number of blinks based on the new valveActiveTimeMS
      Serial.print(F("New led1BlinksToDo: "));
      Serial.println(led1BlinksToDo);
    }
    else
    {
      Serial.println(F("Button long press."));
    }
  }

  lastButtonState = currentButtonState;
}