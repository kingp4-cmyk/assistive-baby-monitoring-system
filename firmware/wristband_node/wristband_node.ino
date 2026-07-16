/*
  Assistive Baby Monitoring System
  Wearable Wristband Node

  Hardware:
  - Wemos D1 Mini / ESP8266
  - Vibration motor
  - NPN transistor and flyback diode
  - Red warning LED

  Communication:
  - ESP-NOW

  Behaviour:
  - Receives cry or inactivity alerts from the crib node.
  - Vibrates once when an alert is accepted.
  - Keeps the warning LED on until power is disconnected.
  - Ignores repeated alerts during the 7-minute cooldown period.

  Important:
  This firmware was reconstructed from the original prototype design.
  It has not been retested on physical hardware.
*/

#include <ESP8266WiFi.h>

extern "C" {
  #include <espnow.h>
  #include <user_interface.h>
}

// --------------------------------------------------
// Pin configuration
// --------------------------------------------------

const uint8_t MOTOR_PIN = D5;
const uint8_t WARNING_LED_PIN = D6;
const uint8_t WIFI_CHANNEL = 1;

// --------------------------------------------------
// Timing settings
// --------------------------------------------------

const unsigned long VIBRATION_DURATION_MS =
    1500UL;

const unsigned long COOLDOWN_DURATION_MS =
    7UL * 60UL * 1000UL;

// --------------------------------------------------
// Alert message
// Must match the crib node message structure.
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

bool vibrationIsActive = false;
bool alertHasBeenAccepted = false;

unsigned long vibrationStartTime = 0;
unsigned long lastAcceptedAlertTime = 0;

// --------------------------------------------------
// Alert handling
// --------------------------------------------------

bool cooldownIsActive(unsigned long currentTime) {
  if (!alertHasBeenAccepted) {
    return false;
  }

  return currentTime - lastAcceptedAlertTime <
         COOLDOWN_DURATION_MS;
}

bool alertTypeIsValid(uint8_t alertType) {
  return alertType == CRY_ALERT ||
         alertType == INACTIVITY_ALERT;
}

void activateAlert(uint8_t alertType) {
  const unsigned long currentTime = millis();

  if (cooldownIsActive(currentTime)) {
    Serial.println(
        "Alert ignored because cooldown is active."
    );
    return;
  }

  // The warning LED remains on until power is removed.
  digitalWrite(WARNING_LED_PIN, HIGH);

  // Start one vibration pulse.
  digitalWrite(MOTOR_PIN, HIGH);
  vibrationIsActive = true;
  vibrationStartTime = currentTime;

  alertHasBeenAccepted = true;
  lastAcceptedAlertTime = currentTime;

  Serial.print("Wristband alert activated: ");

  if (alertType == CRY_ALERT) {
    Serial.println("CRY_ALERT");
  } else {
    Serial.println("INACTIVITY_ALERT");
  }
}

void updateVibration(unsigned long currentTime) {
  if (!vibrationIsActive) {
    return;
  }

  if (currentTime - vibrationStartTime >=
      VIBRATION_DURATION_MS) {
    digitalWrite(MOTOR_PIN, LOW);
    vibrationIsActive = false;

    Serial.println("Vibration pulse completed.");
  }
}

// --------------------------------------------------
// ESP-NOW callback
// --------------------------------------------------

void onDataReceived(
    uint8_t* senderMacAddress,
    uint8_t* incomingData,
    uint8_t dataLength
) {
  if (dataLength != sizeof(AlertMessage)) {
    Serial.println(
        "Invalid ESP-NOW message size."
    );
    return;
  }

  AlertMessage message;
  memcpy(
      &message,
      incomingData,
      sizeof(message)
  );

  if (message.protocolVersion != 1) {
    Serial.println(
        "Unsupported protocol version."
    );
    return;
  }

  if (!alertTypeIsValid(message.alertType)) {
    Serial.println(
        "Invalid alert type received."
    );
    return;
  }

  activateAlert(message.alertType);
}

// --------------------------------------------------
// ESP-NOW setup
// --------------------------------------------------

void initializeEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  wifi_set_channel(WIFI_CHANNEL);

  if (esp_now_init() != 0) {
    Serial.println(
        "ESP-NOW initialization failed."
    );
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataReceived);

  Serial.print("Wristband node MAC address: ");
  Serial.println(WiFi.macAddress());
}

// --------------------------------------------------
// Setup and main loop
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(WARNING_LED_PIN, OUTPUT);

  digitalWrite(MOTOR_PIN, LOW);
  digitalWrite(WARNING_LED_PIN, LOW);

  initializeEspNow();

  Serial.println(
      "Wearable wristband node started."
  );
}

void loop() {
  updateVibration(millis());
  delay(10);
}
