#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

const char* ssid = "IoT-Gateway";    // WiFi SSID
const char* password = "12345678";       // WiFi Password

// This client's fixed identifier - must match one in the server's predefined list
const char* clientID = "device2";        // Use "device1" for the first client

// Define float sensor pins
#define OHT_FLOAT_PIN 25  // Overhead Tank Float Sensor Pin
#define UGT_FLOAT_PIN 26  // Underground Tank Float Sensor Pin

// Track last known states to avoid duplicate messages
int lastOHTState = HIGH;  // Assume initial state as Full
int lastUGTState = HIGH;  // Assume initial state as Full

using namespace websockets;
WebsocketsClient client;

// Function to handle incoming WebSocket messages
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

        // Handle specific events from server
        if (event == "welcome") {
            Serial.print("Server recognized me as: ");
            Serial.println(value);
        }
    }
}

// Function to send JSON messages to WebSocket server
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

void setup() {
    Serial.begin(115200);

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }

    Serial.println("Connected to WiFi!");
    Serial.print("Client IP Address: ");
    Serial.println(WiFi.localIP());

    // Configure float sensor pins as INPUT_PULLUP
    pinMode(OHT_FLOAT_PIN, INPUT_PULLUP);
    pinMode(UGT_FLOAT_PIN, INPUT_PULLUP);

    // Attach WebSocket message callback
    client.onMessage(onMessageCallback);

    // Connect to WebSocket server
    String serverURL = "ws://192.168.4.1:81/?clientId=";
    serverURL += clientID;

    if (client.connect(serverURL)) {
        Serial.println("Connected to WebSocket Server!");
        Serial.print("Using client ID: ");
        Serial.println(clientID);

        // Send initial registration message
        sendJsonMessage("register", clientID);
    } else {
        Serial.println("WebSocket connection failed!");
    }
}

void loop() {
    client.poll();  // Ensure continuous WebSocket communication

    // Read float sensor states
    int ohtState = digitalRead(OHT_FLOAT_PIN);
    int ugtState = digitalRead(UGT_FLOAT_PIN);

    // Check OHT status and send update if it changes
    if (ohtState != lastOHTState) {
        if (ohtState == LOW) {
            sendJsonMessage("OHT_FLOAT", "ON");  // Float grounded → Tank Empty
        } else {
            sendJsonMessage("OHT_FLOAT", "OFF");   // Float not grounded → Tank Full
        }
        lastOHTState = ohtState;
    }

    // Check UGT status and send update if it changes
    if (ugtState != lastUGTState) {
        if (ugtState == LOW) {
            sendJsonMessage("UGT_FLOAT", "ON");  // Float grounded → Tank Empty
        } else {
            sendJsonMessage("UGT_FLOAT", "OFF");   // Float not grounded → Tank Full
        }
        lastUGTState = ugtState;
    }

    // Check if WebSocket connection is lost and reconnect
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
