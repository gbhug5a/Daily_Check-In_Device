// This started as two separate programs, one to use WiFiManager to collect
// WiFi credentials and other needed information, and the second to send
// a push notice via the Pushover app.  But one function does not follow the
// other. If the selected function fails, it just fails. If it succeeds, the
// other function is not performed.

// The selection of which function to perform is determined by the state of
// a GPIO pin connected to another processor.

// The D1 Mini is powered up by the other processor for this purpose, and then
// is powered down.  The project is powered with 5V from a wall wart.


// defines from Config app:

#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>          // tzapu WiFiManager

#include <ArduinoJson.h>

// Name of the config file in LittleFS
const char* configFileName = "/config.json";

// Global config values
char APIkey[40] = "APIkey";   // default
char USERkey[40] = "USERkey";   // default
char GROUPkey[40] = "GROUPkey";   // default
char NAMEkey[40] = "Ada Lovelace";  // default

bool shouldSaveConfig = false;


// additional defines from Pushover app:

String testmsg = " - This is a test notice";
String realmsg = " has failed to check in, or respond to alarms";
String message;
String API;
String WHO;
String USER;
String GROUP;
String NAME;

const int API_TIMEOUT = 15000;
const int httpsPort = 443;
const char* server = "api.pushover.net";         //Pushover server
String pushParameters;                           //keys and message
bool result, saved, nogood;

bool whoFlag = true ;             // true = just to me, false = to Group
bool whatFlag = true;             // true = testmsg, false = realmsg
bool POSTFlag = true;             // true = send Pushover, false = do Config AP

const byte whoPin = D5;          // high if just to me
const byte whatPin = D6;         // high if testmsg
const byte POSTPin = D7;         // high if send Pushover


void loadConfig() {              // load data from LittleFS
  Serial.println("Mounting LittleFS...");
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS, using defaults");
    return;
  }

  if (!LittleFS.exists(configFileName)) {
    Serial.println("Config file not found, using defaults");
    return;
  }

  File configFile = LittleFS.open(configFileName, "r");
  if (!configFile) {
    Serial.println("Failed to open config file, using defaults");
    return;
  }

  size_t size = configFile.size();
  if (size == 0 || size > 1024) {
    Serial.println("Config file size invalid, using defaults");
    configFile.close();
    return;
  }

  // Allocate buffer and parse JSON
  std::unique_ptr<char[]> buf(new char[size + 1]);
  configFile.readBytes(buf.get(), size);
  buf[size] = '\0';
  configFile.close();


  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf.get());
  if (err) {
    Serial.println("Failed to parse config JSON, using defaults");
    return;
  }

  if (doc["value1"].is<const char*>()) {
    strlcpy(APIkey, doc["value1"].as<const char*>(), sizeof(APIkey));
  }
  if (doc["value2"].is<const char*>()) {
    strlcpy(USERkey, doc["value2"].as<const char*>(), sizeof(USERkey));
  }
  if (doc["value3"].is<const char*>()) {
    strlcpy(GROUPkey, doc["value3"].as<const char*>(), sizeof(GROUPkey));
  }
  if (doc["value4"].is<const char*>()) {
    strlcpy(NAMEkey, doc["value4"].as<const char*>(), sizeof(NAMEkey));
  }

  if (POSTFlag) {
    API = APIkey;
    USER = USERkey;
    GROUP = GROUPkey;
    NAME = NAMEkey;
  }
  else {
    Serial.println("Loaded config:");
    Serial.print("value1= ");
    Serial.println(APIkey);
    Serial.print(" value2= ");
    Serial.println(USERkey);
    Serial.print(" value3= ");
    Serial.println(GROUPkey);
    Serial.print(" value4= ");
    Serial.println(NAMEkey);
  }
}

// Called when WiFiManager decides config should be saved
void saveConfigCallback() {
  Serial.println("WiFiManager: should save config");
  shouldSaveConfig = true;
}

// Save custom config to LittleFS
void saveConfig() {
  saved = false;
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS for saving");
    return;
  }
  JsonDocument doc;
  doc["value1"] = APIkey;
  doc["value2"] = USERkey;
  doc["value3"] = GROUPkey;
  doc["value4"] = NAMEkey;

  File configFile = LittleFS.open(configFileName, "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write JSON to config file");
  }
  else {
    saved = true;
    Serial.println("Config saved");
  }

  configFile.close();
}
void setup() {
  pinMode(whoPin, INPUT_PULLUP);
  pinMode(whatPin, INPUT_PULLUP);
  pinMode(POSTPin, INPUT_PULLUP);
  delay(100);
  whoFlag = digitalRead(whoPin);
  whatFlag = digitalRead(whatPin);
  POSTFlag = digitalRead(POSTPin);
  pinMode(whoPin, INPUT);
  pinMode(whatPin, INPUT);
  pinMode(POSTPin, INPUT);

  delay(3000);
  Serial.begin(115200);                          //all Serial ignored if no USB connection
  delay(100);
  WiFi.persistent(true);                         // save Wifi credentials
  nogood = true;
  loadConfig();

  if (POSTFlag) {                                // do POST to Pushover
    if ((strcmp(APIkey, "APIkey") != 0) && connectToWifi()) { // if config and connect ok, send POST to Pushover
      makePushoverRequest();
    }
    WiFi.mode(WIFI_OFF);

    Serial.println("Shutting Down");        // actually just endless loop
  }
  else {                                         // do Config AP
    // Create WiFiManager
    WiFiManager wm;

    // Set callback so we know when to save
    wm.setSaveConfigCallback(saveConfigCallback);

    // Define custom parameters (id, placeholder, default, length)
    WiFiManagerParameter custom_param1("value1", "API Key",
                                       APIkey, 40);
    WiFiManagerParameter custom_param2("value2", "User Key",
                                       USERkey, 40);
    WiFiManagerParameter custom_param3("value3", "Group Key",
                                       GROUPkey, 40);
    WiFiManagerParameter custom_param4("value4", "Your Name",
                                       NAMEkey, 40);

    wm.addParameter(&custom_param1);
    wm.addParameter(&custom_param2);
    wm.addParameter(&custom_param3);
    wm.addParameter(&custom_param4);

    // Optional: timeout for config portal if credentials already exist
    wm.setConfigPortalTimeout(300);
    
    // AP name and password can be customized if desired.
    if (!wm.startConfigPortal("CHECKIN_KEYS")) {
      Serial.println("Failed to connect, or timeout");
      delay(3000);
    }
    else {
      Serial.println("Connected to WiFi");

      // Read updated values from WiFiManager parameters
      strlcpy(APIkey, custom_param1.getValue(), sizeof(APIkey));
      strlcpy(USERkey, custom_param2.getValue(), sizeof(USERkey));
      strlcpy(GROUPkey, custom_param3.getValue(), sizeof(GROUPkey));
      strlcpy(NAMEkey, custom_param4.getValue(), sizeof(NAMEkey));

      // Save config if WiFiManager requested it
      if (shouldSaveConfig) {
        saveConfig();
      }
      LittleFS.end();
      WiFi.mode(WIFI_OFF);
      if (saved) nogood = false;

      Serial.println("Final config:");
      Serial.print("APIkey= ");
      Serial.println(APIkey);
      Serial.print("USERkey= ");
      Serial.println(USERkey);
      Serial.print("GROUPkey= ");
      Serial.println(GROUPkey);
      Serial.print("NAMEkey= ");
      Serial.println(NAMEkey);
    }
  }
  if (nogood == false) {
    digitalWrite(POSTPin, LOW);
    pinMode(POSTPin, OUTPUT);
  }
  while (1) {
    yield();
  }
}

// Establish WiFi connection to the router

bool connectToWifi() {
  WiFi.mode(WIFI_STA);                           //connect as client
  WiFi.begin();                                  //connect to router
  Serial.print("Attempting to connect: ");

  int i = 16;                                    //try to connect for 15 seconds
  while ((WiFi.status() != WL_CONNECTED) && (i-- > 0)) {
    delay(1000);
    Serial.print(i);
    Serial.print(", ");
  }
  Serial.println();

  //print connection result
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected.");
    Serial.print("D1 Mini IP address: ");
    Serial.println(WiFi.localIP());
    result = true;
  }
  else {
    Serial.println("Connection failed - check your credentials or connection");
    result = false;
  }
  return result;
}

// Make an HTTPS request to the Pushover web service

void makePushoverRequest() {
  if (whoFlag) WHO = USER;
  else WHO = GROUP;
  if (whatFlag) message = testmsg;
  else message = realmsg;

  Serial.print("Connecting to ");
  Serial.print(server);
  pushParameters = "token=" + API + "&user=" + WHO + "&message=" + NAME + message;

  WiFiClientSecure client;

  for (int tries = 0; tries < 5; tries++) {      //try up to 5 times to connect
    client.setTimeout(API_TIMEOUT);
    client.setInsecure();                        //don't check certificate/fingerprint
    if (client.connect(server, httpsPort)) break; //exit FOR loop if connection
    Serial.print(".");                           //  else wait, try again
    delay(2000);
  }

  Serial.println();
  if (!client.connected()) {
    Serial.println("Failed to connect to server");
    client.stop();
    return;                                      //if no connection, bail out
  }
  Serial.println("Connected");                   //if good connection, send POST

  int len = pushParameters.length();
  Serial.println("Sending push notification");
  Serial.println(pushParameters);
  Serial.println(len);

  client.println("POST /1/messages.json HTTP/1.1");
  client.println("Host: api.pushover.net");
  client.println("Connection: close\r\nContent-Type: application/x-www-form-urlencoded");
  client.print("Content-Length: ");
  client.print(len);
  client.println("\r\n");
  client.print(pushParameters);

  int timeout = 50;                              //wait 5 seconds for a response
  while (!client.available() && (timeout-- > 0)) {
    delay(100);
  }

  if (!client.available()) {
    Serial.println("No response to POST");
    client.stop();
    return;
  }
  String response;
  response = client.readStringUntil('\n');
  Serial.println(response);                      // should be 200 if all ok

  while (client.available()) {
    // comment one out:
    client.read();                               // skip rest of response, or
    //    Serial.print((char)client.read());           // print rest of response
  }
  Serial.println("\nClosing connection");
  delay(1000);
  client.stop();
  if (response.indexOf("200 OK") >= 0) {
    nogood = false;
    Serial.println("Success!");
  }
}

void loop() {
}
