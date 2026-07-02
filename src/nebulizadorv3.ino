/*
  NEBULIZADOR V3
*/

// Include necessary libraries
#include <DHT.h>
#include "Arduino.h"
#include "pins.h"

// DHT sensor type
const int DHTTYPE = DHT11;
DHT dht(DHTPIN, DHTTYPE);

// Thresholds and constants
const unsigned int LOOP_DELAY_MS = 250; // Delay for the main loop

const float MIN_HINDEX_THRESHOLD = 29.8;
const float MAX_HINDEX_THRESHOLD = 39;
const unsigned long MAX_FREQUENCY_MS = 1800000; // Milliseconds maximum interval for proportional control, adjust as needed
const unsigned long MIN_FREQUENCY_MS = 300000;  // Milliseconds minimum interval for proportional control, adjust as needed

const unsigned long VALVE_ACTIVE_TIME_MS = 5000; // How long the valve stays open per activation

// Global variables
float humidity = 0.0;
float temperature = 0.0;
float hIndex = 0.0;

// Timing variables for non-blocking operation
unsigned long cycleStartTime = 0;
unsigned long currentCycleDelayMs = 0; // Stores the calculated delay for the current cycle
unsigned long sensorReadTime = 0;      // Time of the last sensor read
bool isValveActive = false;

// Setup function
void setup()
{
  // Initialize serial communication
  Serial.begin(9600);

  Serial.println(F("Setup init."));

  // Configure pin modes
  pinMode(SOLENOID_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  // Initialize pins to inactive state
  digitalWrite(SOLENOID_PIN, LOW);
  digitalWrite(LED_BUILTIN, HIGH); // Built-in LED is active-low, HIGH = off

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

  readSensorsLoop(currentTime);

  manageValveLoop(currentTime);

  delay(LOOP_DELAY_MS);
}

// Function to create a light sequence on the built-in LED
void bootSequence()
{
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(LED_BUILTIN, LOW); // LED on
    delay(150);
    digitalWrite(LED_BUILTIN, HIGH); // LED off
    delay(150);
  }
}

/**
 * Function to read sensors in a non-blocking way
 * This function checks if the time since the last sensor read is greater than currentCycleDelayMs
 * If so, it reads the DHT sensor
 */
void readSensorsLoop(unsigned long currentTime)
{
  if (currentTime - sensorReadTime >= currentCycleDelayMs)
  { // Read sensors every currentCycleDelayMs ms
    sensorReadTime = currentTime;
    Serial.println(F("Starting sensor reading..."));
    readDHTSensor();
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
  if (isnan(humidity) || isnan(temperature) || isnan(hIndex))
  {
    if (isValveActive)
    { // If valve was active (e.g. hIndex just dropped), deactivate it
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

  if (hIndex >= MIN_HINDEX_THRESHOLD)
  {
    if (!isValveActive && (currentTime - cycleStartTime >= currentCycleDelayMs))
    {
      // Time to start a new cycle and activate the valve

      // Proportional control: map hIndex range to frequency range
      // Range of hIndex: MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD
      // Range of frequency: MAX_FREQUENCY_MS - MIN_FREQUENCY_MS (inverted: shorter delay for higher hIndex)
      // We want frequency to increase as hIndex increases, so currentCycleDelayMs should decrease.
      // Non-linear mapping using ease-out function: decreases fast at the beginning, slower at the end

      float hIndexRange = MAX_HINDEX_THRESHOLD - MIN_HINDEX_THRESHOLD;
      if (hIndexRange <= 0)
        hIndexRange = 1; // Avoid division by zero or negative

      float factor = (hIndex - MIN_HINDEX_THRESHOLD) / hIndexRange;
      float oneMinusFactor = 1.0 - factor;
      float easeOutFactor = 1.0 - (oneMinusFactor * oneMinusFactor * oneMinusFactor); // Ease-out function (creates the ease-out curve)

      currentCycleDelayMs = MAX_FREQUENCY_MS - (unsigned long)(easeOutFactor * (MAX_FREQUENCY_MS - MIN_FREQUENCY_MS));
      // Clamp the value to be within defined min/max frequencies
      if (currentCycleDelayMs < MIN_FREQUENCY_MS)
        currentCycleDelayMs = MIN_FREQUENCY_MS;
      if (currentCycleDelayMs > MAX_FREQUENCY_MS)
        currentCycleDelayMs = MAX_FREQUENCY_MS;

      Serial.print(F("hIndex ABOVE threshold. Activating valve for "));
      Serial.print(VALVE_ACTIVE_TIME_MS);
      Serial.println(F("ms."));

      Serial.print(F("Next check in "));
      Serial.println(String(currentCycleDelayMs) + F("ms."));

      controlSolenoidValve(true);
      cycleStartTime = currentTime;
    }

    if (isValveActive && (currentTime - cycleStartTime >= VALVE_ACTIVE_TIME_MS))
    {
      // Time to deactivate the valve
      controlSolenoidValve(false);
    }
  }
  else // hIndex < MIN_HINDEX_THRESHOLD
  {
    if (isValveActive)
    { // If valve was active (e.g. hIndex just dropped), deactivate it
      Serial.println(F("hIndex dropped BELOW threshold. Deactivating solenoid valve."));
      controlSolenoidValve(false);
    }
    // Check if it's time to print the status message
    if (currentTime - cycleStartTime >= currentCycleDelayMs)
    {
      Serial.println(F("hIndex BELOW threshold. Valve remains closed."));
      cycleStartTime = currentTime;           // Reset cycle start time
      currentCycleDelayMs = MAX_FREQUENCY_MS; // Reset to max delay as we are in a "calm" state
      Serial.print(F("Next check in "));
      Serial.println(String(currentCycleDelayMs) + F("ms."));
    }
  }
}

// Function to read DHT sensor data
void readDHTSensor()
{
  // Read humidity
  humidity = dht.readHumidity();
  // Read temperature in Celsius (default)
  temperature = dht.readTemperature();

  // Check if any reading failed
  if (isnan(humidity) || isnan(temperature))
  {
    Serial.println(F("Failed to read from DHT sensor!"));
    hIndex = NAN; // Indicate error value
    return;
  }

  // Calculate heat index in Celsius (isFahreheit = false)
  hIndex = dht.computeHeatIndex(temperature, humidity, false);

  // Print values to the serial monitor
  Serial.print(F("Humidity: "));
  Serial.print(humidity);
  Serial.println(F(" %"));

  Serial.print(F("Temperature: "));
  Serial.print(temperature);
  Serial.println(F(" °C"));

  Serial.print(F("Heat Index: "));
  Serial.print(hIndex);
  Serial.println(F(" °C"));
}

// Function to control the solenoid valve
// The solenoid valve is controlled by a MOSFET Trigger Switch Drive Module
// activate: true to open the valve, false to close the valve
void controlSolenoidValve(bool activate)
{
  isValveActive = activate;

  digitalWrite(SOLENOID_PIN, activate ? HIGH : LOW);
  digitalWrite(LED_BUILTIN, activate ? LOW : HIGH); // Built-in LED mirrors the valve (active-low)

  Serial.println(activate ? F("Solenoid valve activated.") : F("Solenoid valve deactivated."));
}
