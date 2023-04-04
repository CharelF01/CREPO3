#include <ETH.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <WiFiClientSecure.h>
#include "API.h"
#include <Wiegand.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "SPIFFS.h"

//#define HOSTNAME "LAMCharityRunFeich127"  //wanted to use this instead of IP but couldn't make it work

//hardcoded wifi parameters for testing
//#define WIFI_SSID "BTS-HUB"
//#define WIFI_PASS "S0meth!ngL33t"

//define mode switch pin and modes
#define MODE_SWITCH 0
#define SECRETARY true
#define RUNNING false

//define pins from rfid scanner
#define RFID_D0 5
#define RFID_D1 4
#define LED 3
#define BUZZER 2
//battery reading pin
#define BATTERY 35
//define on and off for buzzer and led from scanner
#define BUZ_LED_ON LOW
#define BUZ_LED_OFF HIGH


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
//Variables to save values from HTML form
String ssid;
String pass;
// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
//local IP address
IPAddress localIP;

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)

//make api and httpResponse object from api class
API api;
httpResponse resp;
//make wiegand object
Wiegand wiegand;

//save mode of mode switch, default = Running
bool mode = RUNNING;

//save state of ethernet and wifi
bool eth_connected = false;
bool wifi_connected = false;

//save battery percentage and web response string
double batper = 0;
String webresp="";

//function to initialize spiffs
void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}

// Initialize WiFi or check for ethernet
bool initWiFi() {
  if (!eth_connected) {
    if (ssid == "" ) {
      Serial.println("Undefined SSID or IP address.");
      return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.println("Connecting to WiFi...");

    unsigned long currentMillis = millis();
    previousMillis = currentMillis;

    while (WiFi.status() != WL_CONNECTED) {
      currentMillis = millis();
      if (currentMillis - previousMillis >= interval) {
        Serial.println("Failed to connect.");
        return false;
      }
    }
  }
  //WiFi.setHostname(HOSTNAME);
  Serial.println(WiFi.localIP());
  Serial.println(ETH.localIP());
  return true;
}

//also checks if ethernet or wifi is connected at all times
void WiFiEvent(WiFiEvent_t event) {
  if (event == SYSTEM_EVENT_ETH_GOT_IP) eth_connected = true;
  else if (event == SYSTEM_EVENT_STA_GOT_IP) wifi_connected = true;
  else if (event == SYSTEM_EVENT_ETH_DISCONNECTED || event == SYSTEM_EVENT_ETH_STOP) eth_connected = false;
  else if (event == SYSTEM_EVENT_STA_DISCONNECTED || event == SYSTEM_EVENT_STA_STOP || event == SYSTEM_EVENT_STA_LOST_IP) wifi_connected = false;
}
/*
  I noticed that initWiFi and WiFiEvent do nearly the same thing,
  that's why for the future I would eliminate initWiFi() and make
  the necessary changes in the code to make WiFiEvent work, because
  it is much more efficient.
*/


//processes the string which is being sent to the website
String processor(const String& var) {
  if (var == "STATE") {
    if (webresp=="") {
      webresp = "Nobody scanned";
    }
    return webresp;
  }
  return String();
}





void setup() {

  //begin Serial
  Serial.begin(115200);

  //initialize spiffs
  initSPIFFS();

  //begin ethernet
  ETH.begin();
  WiFi.onEvent(WiFiEvent); 

  //set all necessary wiegand events
  wiegand.onReceive(receivedData, "Card read: ");
  wiegand.onReceiveError(receivedDataError, "Card read error: ");
  wiegand.onStateChange(stateChanged, "State changed: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);

  //define pinmodes
  pinMode(BATTERY, INPUT);
  pinMode(RFID_D0, INPUT);
  pinMode(RFID_D1, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(MODE_SWITCH, INPUT_PULLUP);

  //turn buzzer and led off (LED off=RED, on=GREEN)
  digitalWrite(BUZZER, BUZ_LED_OFF);
  digitalWrite(LED, BUZ_LED_OFF);
  

  //attach interrupts for the rfid scanner
  attachInterrupt(digitalPinToInterrupt(RFID_D0), pinStateChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RFID_D1), pinStateChanged, CHANGE);
  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  delay(5000);


  //check if wifi or ethernet are connected
  if (initWiFi()) {
    // root page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    });
    server.serveStatic("/", SPIFFS, "/");

    //write what has been scanned last
    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/index.html", "text/html", false, processor);
    });

    server.begin();
    //log into the api
    resp = api.post("login.php", "username=charun&password=aLowHi!");
  }
  else {
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP("Charity Run WIFI Config", NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL -> redirected to wifimanager
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });

    server.serveStatic("/", SPIFFS, "/");
    //always check if ssid and password have been submitted
    server.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST wifi ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST wifi password value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
        }
      }
      //write the IP on the website where the user can connect after restart
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + String(WiFi.localIP()) + " or " + String(ETH.localIP()));
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }
  

  Serial.print("HTTP Code: ");
  Serial.println(resp.codeHTTP);

  Serial.print("Body: ");
  Serial.println(JSON.stringify(resp.jsonResponse));

  Serial.println("\n");
  delay(10000);
}


void loop() {
  //read the mode switch
  mode = digitalRead(MODE_SWITCH);
  //turn off interrupts to flush wiegand of errors
  noInterrupts();
  wiegand.flush();
  //turn interrupts back on  
  interrupts();
  //tiny delay to assure that the watchdog won't crash
  delay(10);
}

//check if state of a pin changed -> something is being sent
void pinStateChanged() {
  wiegand.setPin0State(digitalRead(RFID_D0));
  wiegand.setPin1State(digitalRead(RFID_D1));
}

//check if wiegand scanner is connected
void stateChanged(bool plugged, const char* message) {
  Serial.print(message);
  Serial.println(plugged ? "CONNECTED" : "DISCONNECTED");
}

// Notifies when a card was read.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onReceive()`
void receivedData(uint8_t* data, uint8_t bits, const char* message) {
  Serial.println(message);
  Serial.print(bits);
  Serial.print("bits / ");
  //Print value in HEX
  uint8_t bytes = (bits + 7) / 8;
  String s;

  //construct the received bytes to hex string
  for (int i = 0; i < bytes; i++) {
    s += String(data[i], HEX);
    Serial.print(data[i] >> 4, 16);
    Serial.print(data[i] & 0xF, 16);
  }
  s.toUpperCase();  //make it all uniform
  Serial.println();
  Serial.println(s);
  String out = "rfid=00" + s; //construction of the outgoing string
  Serial.println(out);

  // FOR TESTING DIFFERENT PEOPLE:
  //String out = "rfid=00B2C8E4ED";  
  //out = "rfid=00B2C8DDED";
  //String out = "rfid=00B2C8E69D";

  //reading battery percentage
  int batval = analogRead(BATTERY);
  batper = constrain(map(batval, 1500 , 2600, 0, 100), 0, 100);
  Serial.println("Battery level: " + String(batper) + "%");
  //changing web response to include battery level
  webresp = "<b><i>Battery level:</b></i> " + String(batper) + " % <br>";
  if (mode == SECRETARY) {
    webresp += "<b><i>Info:</b></i> <br>";
    resp = api.post("info.php", out);
    Serial.println("info");
  } else if (mode == RUNNING) {
    webresp += "<b><i>New Lap counted:</b></i> <br>";
    resp = api.post("lap.php", out);
    Serial.println("lap");
  }

  Serial.print("HTTP Code: ");
  Serial.println(resp.codeHTTP);

  if (resp.codeHTTP != 200) {
    //only rename for easier use
    JSONVar jBody = resp.jsonResponse;
    //change the web response string to error received from the api
    if (jBody.hasOwnProperty("error")) {
      String jERROR = jBody["error"];
      jERROR.replace("\"", "");
      webresp += "<br><i><b>ERROR</b></i>: " + jERROR;
    }
    handleError(resp.codeHTTP);
  } else {
    //only rename for easier use
    JSONVar jBody = resp.jsonResponse;

    //change the web response string to what is received from the api
    if (jBody.hasOwnProperty("name")) {
      String jName = jBody["name"];
      jName.replace("\"", "");
      webresp += "<br><b><i>Runners name:</b></i> " + jName + ", ";
    }
    if (jBody.hasOwnProperty("dossard")) {
      String dossard = jBody["dossard"];
      dossard.replace("\"", "");
      webresp += "<br><b><i>Dossard Number:</b></i> " + dossard + ", ";
    }
    if (jBody.hasOwnProperty("lapCount")) {
      int lapCount = jBody["lapCount"];
      webresp += "<br><b><i>Lap Count:</b></i>  " + String(lapCount) + ", ";
    }
    if (jBody.hasOwnProperty("lapTime")) {
      String lapTime = jBody["lapTime"];
      lapTime.replace("\"", "");
      webresp += "<br><b><i>Last Lap Time:</b></i>  " + lapTime + " Min:Sec, ";
    }
    if (jBody.hasOwnProperty("averageLapTime")) {
      String avgLapTime = jBody["averageLapTime"];
      avgLapTime.replace("\"", "");
      webresp += "<br><b><i>Average Lap Time:</b></i> " + avgLapTime + " Min:Sec, ";
    }
    if (jBody.hasOwnProperty("totalMoney")) {
      int totalMoney = jBody["totalMoney"];
      webresp += "<br><b><i>Total Money earned:</b></i> " + String(totalMoney)+" EUR";
    }
  }
}

// Notifies when an invalid transmission is detected
void receivedDataError(Wiegand::DataError error, uint8_t* rawData, uint8_t rawBits, const char* message) {
  Serial.print(message);
  Serial.print(Wiegand::DataErrorStr(error));
  Serial.print(" - Raw data: ");
  Serial.print(rawBits);
  Serial.print("bits / ");

  //Print value in HEX
  uint8_t bytes = (rawBits + 7) / 8;
  for (int i = 0; i < bytes; i++) {
    Serial.print(rawData[i] >> 4, 16);
    Serial.print(rawData[i] & 0xF, 16);
  }
  handleError(1);
  webresp="Error while scanning card. Try again.";
  Serial.println();
}

//prints error code in serial and notifies the user
void handleError(int errorCode) {
  if (errorCode == 404) {
    Serial.println("Not in database");
    notify(75,5);
  } else if (errorCode == 412) {
    Serial.println("missing/incorrect param");
    notify(40,5);
  } else if (errorCode == 425) {
    Serial.println("scan too early");
    notify(50,5);
  } else if (errorCode == 500) {
    Serial.println("No connection to database");
    notify(30,5);
  }else if(errorCode == 1){
    Serial.println("Error scanning card");
    notify(10,20);
  }

}

//function to beep and blink in different intervalls and repetitions
//according to the error code
void notify(int intervall, int repetitions) {
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED, BUZ_LED_ON);
    digitalWrite(BUZZER, BUZ_LED_ON);
    delay(intervall);
    digitalWrite(LED, BUZ_LED_OFF);
    digitalWrite(BUZZER, BUZ_LED_OFF);
    delay(intervall);
  }
}
