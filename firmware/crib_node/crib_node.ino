/*
  Assistive Baby Monitoring System
  Crib Monitoring Node

  Hardware:
  - Wemos D1 Mini / ESP8266
  - MAX4466 microphone sensor
  - MPU6050 motion sensor
  - Dim crib LED

  Communication:
  - ESP-NOW

  Important:
  This firmware was reconstructed from the original prototype design.
  It has not been retested on physical hardware.
*/

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

extern "C" {
  #include <espnow.h>
  #include <user_interface.h>
}

// --------------------------------------------------
// Pin configuration
// --------------------------------------------------

const uint8_t CRIB_LIGHT_PIN = D5;
const uint8_t WIFI_CHANNEL = 1;

// MAX4466 is connected to the ESP8266 analog input.
const uint8_t MICROPHONE_PIN = A0;

// --------------------------------------------------
// Detection settings
// --------------------------------------------------

// High sound must continue for 10 seconds.
const unsigned long CRY_DURATION_MS =
    10UL * 1000UL;

// No meaningful movement for 20 minutes.
const unsigned long INACTIVITY_LIMIT_MS =
    20UL * 60UL * 1000UL;

// Prevent repeated notifications for 7 minutes.
const unsigned long COOLDOWN_DURATION_MS =
    7UL * 60UL * 1000UL;

// These values must be calibrated for the environment.
const int SOUND_AMPLITUDE_THRESHOLD = 120;
const float MOTION_CHANGE_THRESHOLD = 1.5F;

// --------------------------------------------------
// Receiver MAC addresses
// Replace these values when rebuilding the hardware.
// --------------------------------------------------

uint8_t wristbandAddress[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

uint8_t smartHomeAddress[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// --------------------------------------------------
// Alert message
// --------------------------------------------------

enum AlertType : uint8_t {
  CRY_ALERT = 1,
  INACTIVITY_ALERT = 2
};

struct AlertMessage {
  uint8_t protocolVersion;
  uint8_t alertType;
  uint32_t uptimeMilliseconds;
};

// --------------------------------------------------
// Global state
// --------------------------------------------------

Adafruit_MPU6050 mpu;

bool mpuAvailable = false;
bool previousAccelerationAvailable = false;
bool alertHasBeenSent = false;

float previousAccelerationMagnitude = 0.0F;

unsigned long cryStartTime = 0;
unsigned long lastMovementTime = 0;
unsigned long lastAlertTime = 0;

// --------------------------------------------------
// ESP-NOW functions
// --------------------------------------------------

void onDataSent(uint8_t* macAddress, uint8_t sendStatus) {
  Serial.print("ESP-NOW delivery status: ");

  if (sendStatus == 0) {
    Serial.println("Success");
  } else {
    Serial.println("Failed");
  }
}

bool addPeer(uint8_t* peerAddress) {
  const int result = esp_now_add_peer(
      peerAddress,
      ESP_NOW_ROLE_SLAVE,
      WIFI_CHANNEL,
      nullptr,
      0
  );

  return result == 0;
}

void initializeEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  wifi_set_channel(WIFI_CHANNEL);

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW initialization failed.");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(onDataSent);

  if (!addPeer(wristbandAddress)) {
    Serial.println("Wristband peer could not be added.");
  }

  if (!addPeer(smartHomeAddress)) {
    Serial.println("Smart home peer could not be added.");
  }

  Serial.print("Crib node MAC address: ");
  Serial.println(WiFi.macAddress());
}

void sendAlertToReceivers(AlertType alertType) {
  AlertMessage message;

  message.protocolVersion = 1;
  message.alertType = static_cast<uint8_t>(alertType);
  message.uptimeMilliseconds = millis();

  esp_now_send(
      wristbandAddress,
      reinterpret_cast<uint8_t*>(&message),
      sizeof(message)
  );

  esp_now_send(
      smartHomeAddress,
      reinterpret_cast<uint8_t*>(&message),
      sizeof(message)
  );
}

// --------------------------------------------------
// Sound detection
// --------------------------------------------------

int readSoundAmplitude() {
  const unsigned long sampleStart = millis();

  int minimumReading = 1023;
  int maximumReading = 0;

  while (millis() - sampleStart < 50UL) {
    const int reading = analogRead(MICROPHONE_PIN);

    if (reading < minimumReading) {
      minimumReading = reading;
    }

    if (reading > maximumReading) {
      maximumReading = reading;
    }

    yield();
  }

  return maximumReading - minimumReading;
}

void updateCryDetection(unsigned long currentTime) {
  const int soundAmplitude = readSoundAmplitude();

  if (soundAmplitude >= SOUND_AMPLITUDE_THRESHOLD) {
    if (cryStartTime == 0) {
      cryStartTime = currentTime;
    }

    if (currentTime - cryStartTime >= CRY_DURATION_MS) {
      triggerAlert(CRY_ALERT);
      cryStartTime = 0;
    }
  } else {
    cryStartTime = 0;
  }
}

// --------------------------------------------------
// Motion detection
// --------------------------------------------------

void updateMovementDetection(unsigned long currentTime) {
  if (!mpuAvailable) {
    return;
  }

  sensors_event_t acceleration;
  sensors_event_t gyroscope;
  sensors_event_t temperature;

  mpu.getEvent(
      &acceleration,
      &gyroscope,
      &temperature
  );

  const float x = acceleration.acceleration.x;
  const float y = acceleration.acceleration.y;
  const float z = acceleration.acceleration.z;

  const float magnitude = sqrt(
      (x * x) +
      (y * y) +
      (z * z)
  );

  if (!previousAccelerationAvailable) {
    previousAccelerationMagnitude = magnitude;
    previousAccelerationAvailable = true;
    lastMovementTime = currentTime;
    return;
  }

  const float accelerationChange =
      fabs(magnitude - previousAccelerationMagnitude);

  if (accelerationChange >= MOTION_CHANGE_THRESHOLD) {
    lastMovementTime = currentTime;
  }

  previousAccelerationMagnitude = magnitude;
}

void updateInactivityDetection(unsigned long currentTime) {
  if (!mpuAvailable) {
    return;
  }

  if (currentTime - lastMovementTime >= INACTIVITY_LIMIT_MS) {
    triggerAlert(INACTIVITY_ALERT);

    // Start a new inactivity monitoring period.
    lastMovementTime = currentTime;
  }
}

// --------------------------------------------------
// Alert control
// --------------------------------------------------

bool cooldownIsActive(unsigned long currentTime) {
  if (!alertHasBeenSent) {
    return false;
  }

  return currentTime - lastAlertTime <
         COOLDOWN_DURATION_MS;
}

void triggerAlert(AlertType alertType) {
  const unsigned long currentTime = millis();

  if (cooldownIsActive(currentTime)) {
    return;
  }

  digitalWrite(CRIB_LIGHT_PIN, HIGH);

  sendAlertToReceivers(alertType);

  alertHasBeenSent = true;
  lastAlertTime = currentTime;

  Serial.print("Alert triggered: ");

  if (alertType == CRY_ALERT) {
    Serial.println("CRY_ALERT");
  } else {
    Serial.println("INACTIVITY_ALERT");
  }
}

// --------------------------------------------------
// Setup and main loop
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(CRIB_LIGHT_PIN, OUTPUT);
  digitalWrite(CRIB_LIGHT_PIN, LOW);

  Wire.begin(D2, D1);

  if (mpu.begin()) {
    mpuAvailable = true;

    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    Serial.println("MPU6050 initialized.");
  } else {
    Serial.println(
        "MPU6050 not detected. "
        "Inactivity monitoring is disabled."
    );
  }

  initializeEspNow();

  lastMovementTime = millis();

  Serial.println("Crib monitoring node started.");
}

void loop() {
  const unsigned long currentTime = millis();

  // Movement is still monitored during the cooldown.
  updateMovementDetection(currentTime);

  if (!cooldownIsActive(currentTime)) {
    updateCryDetection(currentTime);
    updateInactivityDetection(currentTime);
  } else {
    cryStartTime = 0;
  }

  delay(20);
}
