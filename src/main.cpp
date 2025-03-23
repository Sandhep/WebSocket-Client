#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

const char* ssid = "IoT-Gateway";
const char* password = "12345678";

// This client's fixed identifier - must match one in the server's predefined list
const char* clientID = "device2";  // Use "device1" for the first client

using namespace websockets;
WebsocketsClient client;

void onMessageCallback(WebsocketsMessage message) {
    Serial.print("Received: ");
    Serial.println(message.data());
    
    // You could add JSON parsing here to handle different responses
    StaticJsonDocument<200> doc;
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

void sendJsonMessage(const char* event, const char* value) {
    StaticJsonDocument<200> doc;
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
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    
    Serial.println("Connected to WiFi!");
    Serial.print("Client IP Address: ");
    Serial.println(WiFi.localIP());
    
    client.onMessage(onMessageCallback);
    
    // Connect with the clientID as a URL parameter
    String serverURL = "ws://192.168.4.1:81/?clientId=";
    serverURL += clientID;
    
    if (client.connect(serverURL)) {
        Serial.println("Connected to WebSocket Server!");
        Serial.print("Using client ID: ");
        Serial.println(clientID);
        
        // Send an initial register message
        sendJsonMessage("register", clientID);
    } else {
        Serial.println("WebSocket connection failed!");
    }
}

void loop() {
    client.poll();  // Ensure continuous WebSocket communication
    
    static unsigned long lastMessageTime = 0;
    if (millis() - lastMessageTime > 3000) {
        // Alternate between different sensor states
        static bool toggleState = false;
        toggleState = !toggleState;
        
        if (toggleState) {
            sendJsonMessage("OHT_FLOAT", "ON");
            delay(500);  // Small delay between messages
            sendJsonMessage("UGT_FLOAT", "ON");
        } else {
            sendJsonMessage("OHT_FLOAT", "OFF");
            delay(500);  // Small delay between messages
            sendJsonMessage("UGT_FLOAT", "OFF");
        }
        
        lastMessageTime = millis();
    }
    
    // Check if connection was lost and reconnect
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