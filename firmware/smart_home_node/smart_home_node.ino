/*
  Assistive Baby Monitoring System
  Smart Home Alert Node

  Hardware:
  - Wemos D1 Mini / ESP8266
  - Visual warning LED

  Communication:
  - ESP-NOW

  Behaviour:
  - Receives cry or inactivity alerts from the crib node.
  - Starts flashing the warning LED.
  - Continues flashing until power is disconnected.
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

const uint8_t WARNING_LED_PIN = D5;
const uint8_t WIFI_CHANNEL = 1;

// --------------------------------------------------
// Timing settings
// --------------------------------------------------

const unsigned long LED_BLINK_INTERVAL_MS = 500UL;

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

bool alertIsActive = false;
bool ledState = false;
bool alertHasBeenAccepted = false;

unsigned long previousBlinkTime = 0;
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

  alertIsActive = true;
  alertHasBeenAccepted = true;
  lastAcceptedAlertTime = currentTime;

  previousBlinkTime = currentTime;
  ledState = true;
  digitalWrite(WARNING_LED_PIN, HIGH);

  Serial.print("Smart home alert activated: ");

  if (alertType == CRY_ALERT) {
    Serial.println("CRY_ALERT");
  } else {
    Serial.println("INACTIVITY_ALERT");
  }
}

void updateWarningLight(unsigned long currentTime) {
  if (!alertIsActive) {
    return;
  }

  if (currentTime - previousBlinkTime >=
      LED_BLINK_INTERVAL_MS) {
    previousBlinkTime = currentTime;
    ledState = !ledState;

    digitalWrite(
        WARNING_LED_PIN,
        ledState ? HIGH : LOW
    );
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

  Serial.print("Smart home node MAC address: ");
  Serial.println(WiFi.macAddress());
}

// --------------------------------------------------
// Setup and main loop
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(WARNING_LED_PIN, OUTPUT);
  digitalWrite(WARNING_LED_PIN, LOW);

  initializeEspNow();

  Serial.println(
      "Smart home alert node started."
  );
}

void loop() {
  updateWarningLight(millis());
  delay(10);
}
