#include <debug_msg.h>
#include <stdio.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoNATS.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <Wire.h>
#include <RtcDS3231.h>
RtcDS3231<TwoWire> Rtc(Wire);

/* Configuration parameters */
#define WIFI_SSID "SkyNet"
#define WIFI_PSK "artinonomet66"
//#define AXON_GATEWAY_HOST "demo.nats.io"
#define AXON_GATEWAY_HOST "192.168.2.103"
#define AXON_GATEWAY_PORT NATS_DEFAULT_PORT /* 4222 */
#define ONE_WIRE_BUS 5
#define TEMPERATURE_PRECISION 9
#define SDA_PORT 2
#define SCL_PORT 14

const String DEVICE_ID = "6cfde020-fcd8-493f-9f6c-d8415b4a3fd5";
const String TOPIC_LOGS = "axon.logs";
const String TOPIC_UPSTREAM = "axon.gateway." + DEVICE_ID; //"6cfde020-fcd8-493f-9f6c-d8415b4a3fd5";
const String TOPIC_DOWNSTREAM = "axon.devices." + DEVICE_ID; // "6cfde020-fcd8-493f-9f6c-d8415b4a3fd5";

WiFiClient client;
NATS nats(
  &client,
  AXON_GATEWAY_HOST,
  AXON_GATEWAY_PORT
);

//class TemperatureSensor temperatureSensor(ONE_WIRE_BUS);
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// arrays to hold device addresses
DeviceAddress insideThermometer = { 0x28, 0x64, 0xE6, 0x83, 0x03, 0x00, 0x00, 0xF2 };
DeviceAddress outsideThermometer = { 0x28, 0xB4, 0x7D, 0xC0, 0x03, 0x00, 0x00, 0x99 };

void setup() {
  Serial.begin(115200);
  delay(10);
  connect_wifi();
  setupRtc(SDA_PORT, SCL_PORT);

  nats.on_connect = nats_on_connect;
  nats.on_error = nats_on_error;
  nats.connect();

  // TEMPERATURE
  // Start up the library
  sensors.begin();

  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");
  //sensors.getAddress(insideThermometer, 0);
  sensors.setResolution(insideThermometer, TEMPERATURE_PRECISION);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connect_wifi();
  }
  checkAlarm();
  nats.process();
  delay(100);
  yield();
}

void connect_wifi() {
  // Connect to WiFi network
  DEBUG_MSG("\n\nConnecting to %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  while (WiFi.status() != WL_CONNECTED) {
    DEBUG_MSG(".");
    delay(100);
    yield();
  }
  DEBUG_MSG("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
}

// TIME =========================================================================

#define countof(a) (sizeof(a) / sizeof(a[0]))
String getTime(String arguments) {
  RtcDateTime now = Rtc.GetDateTime();
  char datestring[20];

  snprintf_P(datestring, 
             countof(datestring),
             PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
             now.Month(),
             now.Day(),
             now.Year(),
             now.Hour(),
             now.Minute(),
             now.Second());

  return String("OK ") + datestring;
}

String setTime(String arguments) {
  int year = 0;
  int month = 0;
  int dayOfMonth = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  arguments = parseInteger(arguments, year);
  arguments = parseInteger(arguments, month);
  arguments = parseInteger(arguments, dayOfMonth);
  arguments = parseInteger(arguments, hour);
  arguments = parseInteger(arguments, minute);
  arguments = parseInteger(arguments, second);

  RtcDateTime newTime = RtcDateTime((uint16_t)year, (uint8_t)month, (uint8_t)dayOfMonth, (uint8_t)hour, (uint8_t)minute, (uint8_t)second);
  Rtc.SetDateTime(newTime);

  return String("OK ");
}

// TIME =========================================================================

// TEMPERATURE ==================================================================
void addressToString(DeviceAddress deviceAddress, String& addrStr)
{
  char buf[3];

  for (uint8_t i = 0; i < 8; i++)
  {
    sprintf(buf, "%02X", deviceAddress[i]);
    addrStr += buf;
  }
}

uint8_t findOneWireDevices()
{
  int deviceCount = sensors.getDeviceCount();
  return deviceCount;
}

void setDIO(const int port, const int state) {
  pinMode(port, OUTPUT);
  digitalWrite(port, state);
}

String result;

String setPort(String arguments) {
  int portNo;
  int state;
  parseInteger(parseInteger(arguments, portNo), state);
  const int newState = (state == 1) ? HIGH : LOW;
  DEBUG_MSG("portNo: %d state: %d", portNo, state);
  setDIO(portNo, newState);
  return "OK";
}

String getDeviceAddress(String arguments) {
  DeviceAddress address;
  int deviceIdx = 0;
  parseInteger(arguments, deviceIdx);
  int deviceCount = findOneWireDevices();
  if (deviceIdx >= 0 && deviceIdx < deviceCount) {
    sensors.getAddress(address, deviceIdx);
    String addrString = "";
    addressToString(address, addrString);
    return String("OK ") + deviceIdx + " " + addrString;
  } else {
    return String("ERR \"Wrong device index ") + deviceIdx + " of 0-" + (deviceCount-1) + "\"";
  }
}

String getDeviceCount(String arguments) {
  int deviceCount = findOneWireDevices();
  return String("OK ") + deviceCount;
}

String getTemperature(String arguments) {
  sensors.requestTemperatures();
  float tempInside = sensors.getTempC(insideThermometer);
  float tempOutside = sensors.getTempC(outsideThermometer);
  return String("OK ") + tempInside + " " + tempOutside;
}

void publishTemperatures(void) {
  RtcDateTime now = Rtc.GetDateTime();
  long int time = now.Epoch64Time();
  sensors.requestTemperatures();
  String tempInside = String(sensors.getTempC(insideThermometer));
  String tempOutside = String(sensors.getTempC(outsideThermometer));
  
  DEBUG_MSG("publish to %s \"temperatures %ld %s %s\"\n", TOPIC_UPSTREAM.c_str(), time, tempInside.c_str(), tempOutside.c_str());
  nats.publishf(TOPIC_UPSTREAM.c_str(), "temperatures %ld %s %s", time, tempInside.c_str(), tempOutside.c_str());
}

// TEMPERATURE ==================================================================

String parseInteger(String str, int& num) {
  num = str.toInt();
  return str.substring(str.indexOf(' ') + 1);
}

String parseString(String str, String& pstr) {
  const int firstSpace = str.indexOf(' ');
  pstr = str.substring(0, firstSpace);
  return str.substring(firstSpace + 1);
}

typedef struct command {
  const char* cmd;
  String (*cmdFun)(String);
} COMMAND;

COMMAND commands[] = {
  {  "setPort",          &setPort          },
  {  "getTemp",          &getTemperature   },
  {  "getDeviceCount",   &getDeviceCount   },
  {  "getDeviceAddress", &getDeviceAddress },
  {  "getTime",          &getTime          },
  {  "setTime",          &setTime          },
  {  0L,                 0L                }
};

const char* processMessage(const char* message) {
  String msgStr = String(message);
  String command;
  String arguments = parseString(msgStr, command);
  DEBUG_MSG("%s\n", arguments.c_str());

  int i = 0;
  while (commands[i].cmd != 0L) {
    if (command.equals(commands[i].cmd)) {
      result = commands[i].cmdFun(arguments);
      break;
    }
    i++;
  }

  if (commands[i].cmd == 0L) {
    // Could reach the end of command table without matching
    return "ERR \"Wrong or missing command\"";
  }
  DEBUG_MSG("%s\n", result.c_str());
  return result.c_str();
}

void sendReply(const char* replyTo, const char* response) {
  DEBUG_MSG("nats.publish \"%s\" to %s\n", response, replyTo);
  nats.publish(replyTo, response);
}

void nats_request_handler(NATS::msg msg) {
  DEBUG_MSG("nats_request_handler called with data: \"%s\" replyTo: %s, size: %d\n", msg.data, msg.reply, msg.size);
  DEBUG_MSG("nats.publish \"nats_request_handler called\" to %s\n", TOPIC_LOGS.c_str());
  nats.publish(TOPIC_LOGS.c_str(), "nats_request_handler called");

  sendReply(msg.reply, processMessage(msg.data));
}

void nats_on_connect() {
  DEBUG_MSG("nats connected and subscribe to %s\n", TOPIC_DOWNSTREAM.c_str());
  nats.subscribe(TOPIC_DOWNSTREAM.c_str(), nats_request_handler);
}

void nats_on_error() {
  DEBUG_MSG("######## nats error occured ################\n");
}

// RTC section ======================================================================

void setupAlarm(void) {
  // Alarm 2 set to trigger at the top of the minute
  DS3231AlarmTwo alarm2(0, 0, 0, DS3231AlarmTwoControl_OncePerMinute);
  Rtc.SetAlarmTwo(alarm2);

  // throw away any old alarm state before we ran
  Rtc.LatchAlarmsTriggeredFlags();
}
    
void setupRtc(int sdaPort, int sclPort) {
  Serial.print("compiled: ");
  Serial.print(__DATE__);
  Serial.println(__TIME__);

  Wire.begin(sdaPort, sclPort); // due to limited pins, use pin 0 and 2 for SDA, SCL
    
  Rtc.Begin();

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  printDateTime(compiled);
  Serial.println();

  if (!Rtc.IsDateTimeValid()) {
    // Common Cases:
    //    1) first time you ran and the device wasn't running yet
    //    2) the battery on the device is low or even missing
    Serial.println("RTC lost confidence in the DateTime!");

    // following line sets the RTC to the date & time this sketch was compiled
    // it will also reset the valid flag internally unless the Rtc device is having an issue
    Rtc.SetDateTime(compiled);
  }

  if (!Rtc.GetIsRunning()) {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled) {
      Serial.println("RTC is older than compile time!  (Updating DateTime)");
      Rtc.SetDateTime(compiled);
  } else if (now > compiled) {
      Serial.println("RTC is newer than compile time. (this is expected)");
  } else if (now == compiled) {
      Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }

  setupAlarm();
  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone); 
}

void printDateTime(const RtcDateTime& dt) {
  char datestring[20];

  snprintf_P(datestring, 
          countof(datestring),
          PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
          dt.Month(),
          dt.Day(),
          dt.Year(),
          dt.Hour(),
          dt.Minute(),
          dt.Second());
  Serial.print(datestring);
}

void checkAlarm(void) {
  DS3231AlarmFlag flag = Rtc.LatchAlarmsTriggeredFlags();
  if (flag & DS3231AlarmFlag_Alarm2)
  {
    Serial.println("alarm two triggered");
    publishTemperatures();
  }
}

// RTC section ======================================================================

