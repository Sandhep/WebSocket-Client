#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define EEPROM_SIZE 2  // Storing OHT and UGT states

const char* ssid = "IoT-Gateway";
const char* password = "12345678";

const char* clientID = "device2";  // Use "device1" for the first client

// Float Sensor Pins
#define OHT_FLOAT_PIN 25  // Overhead Tank Float Sensor Pin
#define UGT_FLOAT_PIN 26  // Underground Tank Float Sensor Pin
#define PUMP_RELAY_PIN 27 // Relay Pin for Pump Control

// Default Float States
int lastOHTState;
int lastUGTState;

// Pump and Mode States
String mode = "auto";  // Default to auto mode
bool pumpState = false;

using namespace websockets;
WebsocketsClient client;

// --------------------------
// WebSocket Callback Handler
// --------------------------
void onMessageCallback(WebsocketsMessage message) {
    Serial.print("Received: ");
    Serial.println(message.data());

    // Parse incoming JSON message
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message.data());

    if (!error) {
        String event = doc["event"];
        String value = doc["value"];

        Serial.print("Event: ");
        Serial.print(event);
        Serial.print(", Value: ");
        Serial.println(value);

        if (event == "mode") {
            mode = value;
            Serial.print("Mode switched to: ");
            Serial.println(mode);
        }

        // Manual mode pump control
        if (mode == "manual" && event == "pump") {
            if (value == "on" && digitalRead(UGT_FLOAT_PIN) == HIGH) {
                digitalWrite(PUMP_RELAY_PIN, HIGH);
                pumpState = true;
                Serial.println("Pump turned ON (Manual Mode)");
            } else if (value == "off") {
                digitalWrite(PUMP_RELAY_PIN, LOW);
                pumpState = false;
                Serial.println("Pump turned OFF (Manual Mode)");
            }
        }
    }
}

// --------------------------
// Send JSON Message to Server
// --------------------------
void sendJsonMessage(const char* event, const char* value) {
    JsonDocument doc;
    doc["event"] = event;
    doc["value"] = value;

    String jsonString;
    serializeJson(doc, jsonString);

    client.send(jsonString);
    Serial.print("Sent: ");
    Serial.println(jsonString);
}

// --------------------------
// Save Float State to EEPROM
// --------------------------
void saveFloatState(int ohtState, int ugtState) {
    EEPROM.write(0, ohtState);
    EEPROM.write(1, ugtState);
    EEPROM.commit();
}

// --------------------------
// Setup Function
// --------------------------
void setup() {
    Serial.begin(115200);
    EEPROM.begin(EEPROM_SIZE);

    // Load last float states from EEPROM
    lastOHTState = EEPROM.read(0);
    lastUGTState = EEPROM.read(1);

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi!");
    Serial.print("Client IP Address: ");
    Serial.println(WiFi.localIP());

    // Configure float and relay pins
    pinMode(OHT_FLOAT_PIN, INPUT_PULLUP);
    pinMode(UGT_FLOAT_PIN, INPUT_PULLUP);
    pinMode(PUMP_RELAY_PIN, OUTPUT);
    digitalWrite(PUMP_RELAY_PIN, LOW);  // Pump OFF initially

    // Attach WebSocket callback
    client.onMessage(onMessageCallback);

    // Connect to WebSocket server
    String serverURL = "ws://192.168.4.1:81/?clientId=";
    serverURL += clientID;

    if (client.connect(serverURL)) {
        Serial.println("Connected to WebSocket Server!");
        sendJsonMessage("register", clientID);
    } else {
        Serial.println("WebSocket connection failed!");
    }
}

// --------------------------
// Main Loop
// --------------------------
void loop() {
    client.poll();  // WebSocket keep-alive

    // Read current float sensor states
    int ohtState = digitalRead(OHT_FLOAT_PIN);
    int ugtState = digitalRead(UGT_FLOAT_PIN);

    // Check and update OHT state if changed
    if (ohtState != lastOHTState) {
        if (ohtState == LOW) {
            sendJsonMessage("OHT_FLOAT", "ON");  // Tank Empty
        } else {
            sendJsonMessage("OHT_FLOAT", "OFF");  // Tank Full
        }
        lastOHTState = ohtState;
        saveFloatState(ohtState, lastUGTState);
    }

    // Check and update UGT state if changed
    if (ugtState != lastUGTState) {
        if (ugtState == LOW) {
            sendJsonMessage("UGT_FLOAT", "ON");  // Tank Empty
        } else {
            sendJsonMessage("UGT_FLOAT", "OFF");  // Tank Full
        }
        lastUGTState = ugtState;
        saveFloatState(lastOHTState, ugtState);
    }

    // ---------
    // Auto Mode Logic
    // ---------
    if (mode == "auto") {
        if (ohtState == LOW && ugtState == HIGH) {
            digitalWrite(PUMP_RELAY_PIN, HIGH);  // Pump ON
            if (!pumpState) {
                sendJsonMessage("PUMP", "ON");
                pumpState = true;
            }
        } else {
            digitalWrite(PUMP_RELAY_PIN, LOW);  // Pump OFF
            if (pumpState) {
                sendJsonMessage("PUMP", "OFF");
                pumpState = false;
            }
        }
    }

    // -----------
    // Reconnect if WebSocket is Disconnected
    // -----------
    if (!client.available()) {
        Serial.println("Connection lost, reconnecting...");
        String serverURL = "ws://192.168.4.1:81/?clientId=";
        serverURL += clientID;

        if (client.connect(serverURL)) {
            Serial.println("Reconnected to WebSocket Server!");
            sendJsonMessage("register", clientID);
        } else {
            Serial.println("Reconnection failed");
            delay(5000);  // Wait before trying again
        }
    }
}
