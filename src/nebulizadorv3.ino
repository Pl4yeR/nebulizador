/*
  NEBULIZADOR V3
*/

// Include necessary libraries
#include <DHT.h>

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
const unsigned long LOOP_DELAY_MS = 300; // Delay for the main loop
const int LUMINOSITY_ERROR_VALUE = -1;   // Constant for luminosity error
const float MIN_HINDEX_THRESHOLD = 30.0;
const float MAX_HINDEX_THRESHOLD = 38.0;
const unsigned long MAX_FREQUENCY_MS = 300000; // Seconds maximum interval for proportional control, adjust as needed
const unsigned long MIN_FREQUENCY_MS = 60000;  // Seconds minimum interval for proportional control, adjust as needed

// Global variables
float humidity = 0.0;
float temperature = 0.0;
float hIndex = 0.0;
int luminosity = 0.0;
unsigned long valveActiveTimeMS = 5000; // 5 seconds default
unsigned long lastButtonPressTime = 0;  // For button debouncing
int lastButtonState = HIGH;             // Assuming pull-up resistor, so HIGH when not pressed
bool buttonPressed = false;             // Flag to indicate a valid button press

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

  if (currentTime - sensorReadTime >= currentCycleDelayMs)
  { // Read sensors every currentCycleDelayMs ms
    sensorReadTime = currentTime;
    readDHTSensor();
    readLuminositySensor();
  }

  // Always Read button state
  readButton();

  // Check for sensor errors
  if (isnan(humidity) || isnan(temperature) || luminosity == LUMINOSITY_ERROR_VALUE)
  {
    Serial.println(F("Error reading sensors. Skipping valve control."));
    delay(LOOP_DELAY_MS);
    return;
  }

  Serial.print(F("Current hIndex: "));
  Serial.println(hIndex);

  if (hIndex >= MIN_HINDEX_THRESHOLD)
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

      Serial.print(F("hIndex ABOVE minimum threshold. Activating valve for "));
      Serial.print(valveActiveTimeMS);
      Serial.println(F("ms."));

      controlSolenoidValve(true);
      digitalWrite(LED_STATUS_1, HIGH);
      isValveActive = true;
      cycleStartTime = currentTime;
    }

    if (isValveActive && (currentTime - cycleStartTime >= valveActiveTimeMS))
    {
      // Time to deactivate the valve
      controlSolenoidValve(false);
      digitalWrite(LED_STATUS_1, LOW);
      isValveActive = false;
    }
  }
  else // hIndex < MIN_HINDEX_THRESHOLD
  {
    if (isValveActive)
    { // If valve was active (e.g. hIndex just dropped), deactivate it
      Serial.println(F("hIndex dropped BELOW threshold. Deactivating solenoid valve."));
      controlSolenoidValve(false);
      digitalWrite(LED_STATUS_1, LOW);
      isValveActive = false;
    }
    // Check if it's time to print the status message
    if (currentTime - cycleStartTime >= currentCycleDelayMs)
    {
      Serial.println(F("hIndex BELOW minimum threshold. Valve remains closed."));
      cycleStartTime = currentTime;
      currentCycleDelayMs = MAX_FREQUENCY_MS;
    }
  }

  delay(LOOP_DELAY_MS);
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

// Function to read button state and update valveActiveTimeMS
void readButton()
{
  int currentButtonState = digitalRead(BUTTON_PIN);

  // Debounce logic
  if (currentButtonState != lastButtonState)
  {
    lastButtonPressTime = millis();
  }

  if ((millis() - lastButtonPressTime) > 50)
  { // 50ms debounce delay
    if (currentButtonState == LOW && lastButtonState == HIGH && !buttonPressed)
    {                       // Button pressed (LOW state due to INPUT_PULLUP)
      buttonPressed = true; // Mark as pressed to avoid multiple increments per press
      Serial.println(F("Button pressed."));
      valveActiveTimeMS += 5000;
      if (valveActiveTimeMS > 20000)
      {
        valveActiveTimeMS = 5000;
      }
      Serial.print(F("New valveActiveTimeMS: "));
      Serial.println(valveActiveTimeMS);
    }
    else if (currentButtonState == HIGH)
    {
      buttonPressed = false; // Reset button pressed flag when released
    }
  }
  lastButtonState = currentButtonState;
}