#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

const char* ssid = "IoT-Gateway";    // WiFi SSID
const char* password = "12345678";       // WiFi Password

// This client's fixed identifier - must match one in the server's predefined list
const char* clientID = "device1";        // Use "device1" for the first client

// Pin definitions
const int OVERHEAD_TANK_PIN = 19;  
const int UNDERGROUND_TANK_PIN = 18;  
const int Pump = 26;

// ISR variables 

volatile bool buttonPressed_OHT = false;
volatile unsigned long lastInterruptTime_OHT = 0;

volatile bool buttonPressed_UGT = false;
volatile unsigned long lastInterruptTime_UGT = 0;

const unsigned long debounceTime = 200; // 200 ms debounce time

volatile bool OHT_State = false;
volatile bool UGT_State = false;

// Track last known states to avoid duplicate messages
int lastOHTState = HIGH;  // Assume initial state as Full
int lastUGTState = HIGH;  // Assume initial state as Full

String mode = "MANUAL";  // Default to Manual mode
String timer = "DISABLED"; // Default to Disabled
String timerOut = "OFF"; // Default OFF

using namespace websockets;
WebsocketsClient client;

void autoLogic();
void controlPump(String state,String event);
void startupSequence();
void onMessageCallback(WebsocketsMessage message);
void sendJsonMessage(const char* event, const char* value);
void IRAM_ATTR OHT_ISR();
void IRAM_ATTR UGT_ISR(); 
// Setup Section   

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

    pinMode(OVERHEAD_TANK_PIN, INPUT_PULLUP);  // Enable pull-up for overhead tank
    pinMode(UNDERGROUND_TANK_PIN, INPUT_PULLUP); // Enable pull-up for underground tank
    attachInterrupt(digitalPinToInterrupt(OVERHEAD_TANK_PIN), OHT_ISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(UNDERGROUND_TANK_PIN), UGT_ISR, CHANGE);

    OHT_State = digitalRead(OVERHEAD_TANK_PIN);
    UGT_State = digitalRead(UNDERGROUND_TANK_PIN);

    pinMode(Pump,OUTPUT);
    digitalWrite(Pump,HIGH);

    // Attach WebSocket message callback
    client.onMessage(onMessageCallback);

    // Connect to WebSocket server
    String serverURL = "ws://192.168.4.1:81/?clientId=";
    serverURL += clientID;

    if (client.connect(serverURL)) {
        Serial.println("Connected to WebSocket Server!");
        Serial.print("Using client ID: ");
        Serial.println(clientID);
        startupSequence();

    } else {
        Serial.println("WebSocket connection failed!");
    }
}

void loop() {
    client.poll();  // Ensure continuous WebSocket communication

    if (buttonPressed_OHT) {

        Serial.println("OHT Float Position Changed!");
        delay(500);
        OHT_State = digitalRead(OVERHEAD_TANK_PIN);
        sendJsonMessage("OHT_FLOAT", OHT_State ? "OFF":"ON");
        buttonPressed_OHT = false;

        if(mode == "AUTO"){
            autoLogic();
        }
    }
    
    if (buttonPressed_UGT) {

        Serial.println("UGT Float Position Changed!");
        delay(500);
        UGT_State = digitalRead(UNDERGROUND_TANK_PIN);
        sendJsonMessage("UGT_FLOAT",  UGT_State ? "OFF":"ON");
        buttonPressed_UGT = false;

        if(mode == "AUTO"){
            autoLogic();
        }
    }

    // Check if WebSocket connection is lost and reconnect

    if (!client.available()) {
        Serial.println("Connection lost, reconnecting...");

        String serverURL = "ws://192.168.4.1:81/?clientId=";
        serverURL += clientID;

        if (client.connect(serverURL)) {
            Serial.println("Reconnected to WebSocket Server!");
            startupSequence();
        } else {
            Serial.println("Reconnection failed");
            delay(5000);  // Wait before trying again
        }
    }
}


// Auto Logic

void autoLogic(){

        // Read current float states again to ensure fresh data
        OHT_State = digitalRead(OVERHEAD_TANK_PIN);
        UGT_State = digitalRead(UNDERGROUND_TANK_PIN);

        String autoOut = "OFF";

        if (OHT_State == LOW && UGT_State == HIGH) {
            // UGT Full & OHT Empty → Turn pump ON
            autoOut = "ON";
        }

        if(timer == "ENABLED" && mode == "AUTO"){
            if(timerOut == "ON" && autoOut == "ON"){
                controlPump("ON","Timer and Auto");
            }else{
                controlPump("OFF","Timer and Auto");
            }
        }else if(timer == "ENABLED" && mode == "MANUAL"){
            if(timerOut == "ON"){
                controlPump("ON","Timer");
            }else{
                controlPump("OFF","Timer");
            }
        }else if(mode == "AUTO" && timer == "DISABLED"){
            if(autoOut == "ON"){
                controlPump("ON","Auto");
            }else{
                controlPump("OFF","Auto");
            }
        }
}

// Pump Control

void controlPump(String state,String event){

    digitalWrite(Pump, state == "ON" ? LOW : HIGH);  
    sendJsonMessage("PumpState", state == "ON" ? "ON" : "OFF");
    char buffer[50];
    sprintf(buffer, "%s MODE: Pump turned %s", event, state);
    Serial.println(buffer);    

}

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

        }else if (event == "Mode") {

            mode = value;
            Serial.print("Mode switched to: ");
            Serial.println(mode);

            autoLogic();

        }else if(event == "Pump_State" && mode == "MANUAL"){

            digitalWrite(Pump, value == "ON" ? LOW : HIGH);

        }else if(event == "Timer"){

             timer = value;
             Serial.print("Timer is: ");
             Serial.println(timer);

             autoLogic();

        }else if(event == "TimerOut"){

             timerOut = value;
             Serial.print("Timer is");
             Serial.println(timerOut == "ON"? "Started" : "Stopped");
             
             autoLogic();
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

// ISR for Float Switches

void IRAM_ATTR OHT_ISR() {
    unsigned long currentTime = millis();
    if (currentTime - lastInterruptTime_OHT > debounceTime) {
      buttonPressed_OHT = true;
      lastInterruptTime_OHT = currentTime;
    }
}
  
  
void IRAM_ATTR UGT_ISR() {
    unsigned long currentTime = millis();
    if (currentTime - lastInterruptTime_UGT > debounceTime) {
      buttonPressed_UGT = true;
      lastInterruptTime_UGT = currentTime;
    }
}

void startupSequence(){
    sendJsonMessage("register", clientID);
    sendJsonMessage("OHT_FLOAT", OHT_State ? "OFF":"ON");
    sendJsonMessage("UGT_FLOAT",  UGT_State ? "OFF":"ON");
}
  