#include <FS.h>          // this needs to be first, or it all crashes and burns...
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson

#include <SimpleTimer.h>    //https://github.com/marcelloromani/Arduino-SimpleTimer/tree/master/SimpleTimer
#include <ESP8266WiFi.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <ESP8266mDNS.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <PubSubClient.h>   //https://github.com/knolleary/pubsubclient
#include <AccelStepper.h>  
#include <ArduinoOTA.h>     //https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA

/*****************  START USER CONFIG SECTION *********************************/

#define MQTT_CLIENT_ROOT_NAME     "BlindsMCU/"  // Used to define MQTT topics, MQTT Client ID, and ArduinoOTA
#define ROOTLEN (sizeof(MQTT_CLIENT_ROOT_NAME) -1)

#define STEPPER_MAX_SPEED         400                 //Defines the maximum speed in stesp/sec for your stepper motor
#define STEPPER_SPEED             300                 //Defines the requestedspeed in steps/sec for your stepper motor
#define STEPPER_ACCEL             50                  //Defines the accelleration for your stepper motor
#define STEPPER_STEPS_PER_REV     3072                //Defines the number of pulses that is required for the stepper to rotate 360 degrees
#define FULLSTEP 4

#define CLOSED_UP_POSITION 0
#define OPEN_POSITION 2100
#define CLOSED_DOWN_POSITION 4200


#define STEPPER_IN1          D5
#define STEPPER_IN2          D6
#define STEPPER_IN3          D7
#define STEPPER_IN4          D8
 
/*****************  END USER CONFIG SECTION *********************************/

WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient);
SimpleTimer timer;
AccelStepper stepper(FULLSTEP, STEPPER_IN1, STEPPER_IN3, STEPPER_IN2, STEPPER_IN4);

//Global Variables
char positionPublish[50];
char tmpPublish[50];
bool command_active = true;

char mqtt_server[40];
char *mqtt_port = "1883" ;
char mqtt_user[40];
char mqtt_pass[40];
char mqtt_client_name[40] = MQTT_CLIENT_ROOT_NAME;


//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setupSpiffs(){
  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument json(1024);
        auto error = deserializeJson(json, buf.get());
        if (error) {
          Serial.print(F("deserializeJson() failed with code "));
          Serial.println(error.c_str());
          return;
        }

        serializeJson(json, Serial);
        Serial.println("\nparsed json");

        strcpy(mqtt_server, json["mqtt_server"]);
        strcpy(mqtt_port, json["mqtt_port"]);
        strcpy(mqtt_user, json["mqtt_user"]);
        strcpy(mqtt_pass, json["mqtt_pass"]);
        strcpy(mqtt_client_name + ROOTLEN, json["mqtt_client_name"]);
        Serial.println(mqtt_client_name + ROOTLEN);
        Serial.println(mqtt_client_name);

        // Preload publishing buffer
        strncpy(tmpPublish, mqtt_client_name, sizeof(tmpPublish));
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}

void reconnect(bool boot) 
{
  int retries = 0;
  while (!client.connected()) {
    retries++;
    if(retries < 50)
    {
      Serial.print("Attempting MQTT connection...");
      if (client.connect(mqtt_client_name, mqtt_user, mqtt_pass)) 
      {
        Serial.println("connected");
        strncpy(tmpPublish + strlen(mqtt_client_name), "/checkIn", 50); 
        if(boot == false)
        {
          client.publish(tmpPublish, "Reconnected"); 
        }
        if(boot == true)
        {
          client.publish(tmpPublish, "Rebooted");
        }
        // ... and resubscribe
        strcpy(tmpPublish + strlen(mqtt_client_name), "/blindsCommand"); 
        client.subscribe(tmpPublish);
        strcpy(tmpPublish + strlen(mqtt_client_name), "/positionCommand"); 
        client.subscribe(tmpPublish);
      } 
      else 
      {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    if(retries > 49)
    {
    ESP.restart();
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message arrived [");
  String newTopic = (topic + strlen(mqtt_client_name));
  Serial.print(topic);
  Serial.print("] ");
  payload[length] = '\0';
  String newPayload = String((char *)payload);
  int intPayload = newPayload.toInt();
  Serial.println(newPayload);
  Serial.println();
  if (newTopic == "/blindsCommand") 
  {
    if (newPayload == "OPEN")
    {
      setZero();
      stepper.moveTo(OPEN_POSITION);
      command_active = true;
    }
    else if (newPayload == "CLOSE")
    {   
      setZero();
      stepper.moveTo(CLOSED_UP_POSITION);
      command_active = true;
    }
    else if (newPayload == "STOP")
    {
      stepper.stop();
      command_active = true;
    }
  }
  if (newTopic == "/positionCommand")
  {
    stepper.moveTo(intPayload);
    command_active = true;
  }
  if (newTopic == "/setPositionCommand")
  {
    stepper.setCurrentPosition(intPayload);
    command_active = true;
  }
  
}

void publishPosition()
{
  command_active = false;
  stepper.disableOutputs();
  String temp_str = String(stepper.currentPosition());
  temp_str.toCharArray(positionPublish, temp_str.length() + 1);
  strcpy(tmpPublish + strlen(mqtt_client_name), "/positionState"); 
  client.publish(tmpPublish, positionPublish); 
}

void checkIn()
{
  strcpy(tmpPublish + strlen(mqtt_client_name), "/checkIn"); 
  client.publish(tmpPublish,"OK"); 
}

void setZero()
{
  stepper.moveTo(-CLOSED_DOWN_POSITION);
  stepper.runToPosition();
  stepper.setCurrentPosition(0);
}

void setup() {
  Serial.begin(115200);

  // Setup stepper
  stepper.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper.setAcceleration(STEPPER_ACCEL);
  stepper.setSpeed(STEPPER_SPEED);
  setZero();

  setupSpiffs();
  wm.setSaveConfigCallback(saveConfigCallback);
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", "", 32);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", "", 32);
  WiFiManagerParameter custom_mqtt_client_name("client_name", "mqtt client name", mqtt_client_name + ROOTLEN, 32);

  // MQTT params
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_client_name);

  wm.autoConnect("AutoConnectAP");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  strcpy(mqtt_client_name + ROOTLEN, custom_mqtt_client_name.getValue());
  // Preload publishing buffer
  strncpy(tmpPublish, mqtt_client_name, sizeof(tmpPublish));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;
    json["mqtt_client_name"] = mqtt_client_name + ROOTLEN;


    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJsonPretty(json, Serial);
    serializeJson(json, configFile);
    configFile.close();
    //end save
    shouldSaveConfig = false;
  }

  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(callback);
  reconnect(true);

  ArduinoOTA.setHostname(mqtt_client_name);
  ArduinoOTA.begin(); 
  delay(10);
  
  timer.setInterval(90000, checkIn);

}

void loop() 
{
  if (!client.connected()) 
  {
    reconnect(false);
  }
  client.loop();
  ArduinoOTA.handle();
  timer.run();
  stepper.run();
  if (command_active && !stepper.isRunning()) publishPosition();
}
