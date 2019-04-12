#include <ESP8266WiFi.h>
#include <time.h>      // time() ctime()
#include <sys/time.h>  // struct timeval
#include <coredecls.h> // settimeofday_cb()
#include <PubSubClient.h>
#include <DHTesp.h>
#include <vector>
#include <string>

#define TZ -5    // (utc+) TZ in hours
#define DST_MN 0 // use 60mn for summer time in some countries

#define RTC_TEST 1510592825 // 1510592825 = Monday 13 November 2017 17:07:05 UTC

#define TZ_MN ((TZ)*60)
#define TZ_SEC ((TZ)*3600)
#define DST_SEC ((DST_MN)*60)

#define DHT_PORT 2

#define MAX_FRAME_SIZE 256
#define START 0xBE
#define END 0x0D

// Update these with values suitable for your network.
const char *ssid = "";
const char *password = "";

// MQTT settings
const char *mqtt_server = "m16.cloudmqtt.com";
const int mqtt_port = 12051;
const char *mqtt_user = "";
const char *mqtt_password = "";
const char *mqtt_in_topic = "in";
const char *mqtt_out_topic = "out";

WiFiClient espClient;
PubSubClient client(espClient);

DHTesp *dht;

// Time NTP variables
timeval cbtime; // time set in callback
bool cbtime_set = false;

void time_is_set(void)
{
  gettimeofday(&cbtime, NULL);
  cbtime_set = true;
  Serial.println("------------------ settimeofday() was called ------------------");
}

std::vector<uint8_t> createFrame(const char *data);
std::vector<uint8_t> deviceGetId();
std::string intToString(int num, int base = 10);

void setup()
{
  // Config the NTP
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

  // Initialize the BUILTIN_LED pin as an output
  //pinMode(BUILTIN_LED, OUTPUT);

  Serial.begin(115200);

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  settimeofday_cb(time_is_set);

  dht = new DHTesp();

  dht->setup(DHT_PORT, DHTesp::DHT22);
}

void setup_wifi()
{

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1')
  {
    digitalWrite(BUILTIN_LED, LOW); // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  }
  else
  {
    digitalWrite(BUILTIN_LED, HIGH); // Turn the LED off by making the voltage HIGH
  }
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client", mqtt_user, mqtt_password))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(mqtt_out_topic, "hello world");
      // ... and resubscribe
      client.subscribe(mqtt_in_topic);
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
}

void loop()
{

  if (!client.connected())
  {
    reconnect();
  }

  client.loop();

  TempAndHumidity newValues = dht->getTempAndHumidity();
  // Check if any reads failed and exit early (to try again).
  if (dht->getStatus() != 0)
  {
    Serial.println("DHT22 status: " + String(dht->getStatusString()));
    delay(2000);
    return;
  }

  float heatIndex = dht->computeHeatIndex(newValues.temperature, newValues.humidity);

  if (newValues.humidity == 0.00 && newValues.temperature == 0.00)
  {
    return;
  }

  std::string _date, _time, _data;
  char *lectura;
  lectura = (char *)realloc(lectura, 21);

  // Format the output: <Humidity,Temperature,HIC> "00.00,00.00,00.00"
  sprintf(lectura, "%02.2f,%02.2f,%02.2f,%d", newValues.humidity, newValues.temperature, heatIndex, 21);

  // Date Format YYYY:MM:DD
  _date = getDate();

  // Time Format HH:MM:SS
  _time = getTime();

  // Data Format: date,time,dht: YYYY:MM:DD,HH:MM:SS,00.00,00.00,00.00
  _data = _date + "," + _time + "," + std::string(lectura);

  //Serial.println(_data.c_str());
  //char str[6];

  // ---------------------- Create Frame ----------------------
  std::vector<uint8_t> frame;
  frame = createFrame(_data.c_str());

  //std::ostringstream oss;

  Serial.print("Publish message: ");

  // Conver the vector into a string
  std::string _out(frame.begin(), frame.end());

  Serial.println(_out.c_str());
  client.publish(mqtt_out_topic, _out.c_str());

  delay(10000);
}

std::string getDate()
{
  time_t now;
  now = time(nullptr);

  // 2019-04-02
  char buffer[11];
  struct tm *curr_tm;

  curr_tm = localtime(&now);
  strftime(buffer, sizeof(buffer), "%F", curr_tm);

  return std::string(buffer);
}

std::string getTime()
{
  time_t now;
  now = time(nullptr);

  // 20:55:29
  char buffer[9];
  struct tm *curr_tm;

  curr_tm = localtime(&now);
  strftime(buffer, sizeof(buffer), "%T", curr_tm);

  return std::string(buffer);
}

std::string getTimestamp()
{

  return getDate() + " " + getTime();
}

std::vector<uint8_t> createFrame(const char *data)
{
  std::vector<uint8_t> frame_buff;
  int frame_length = 26;
  int i, j;
  std::vector<uint8_t> identifier;
  std::string _type = "101";

  frame_length += strlen(data);

  // Start: [1-2]
  frame_buff.push_back(69);
  frame_buff.push_back(78);

  // Adding a comma separator [3]
  frame_buff.push_back(44);

  // Adding the length [4-5]
  std::string str_frame_length;
  str_frame_length = intToString(frame_length);
  for (i = 0; i < str_frame_length.size(); i++)
  {
    frame_buff.push_back(str_frame_length.at(i));
  }

  // Adding a comma separator [6]
  frame_buff.push_back(44);

  // Add the serial number to the frame [7-18]
  identifier = deviceGetId();
  for (i = 0; i < identifier.size(); i++)
  {
    frame_buff.push_back(identifier.at(i));
  }

  // Adding a comma separator [19]
  frame_buff.push_back(44);

  // Add the code of the frame [20-22]
  for (i = 12, j = 0; j < _type.length(); i++, j++)
  {
    frame_buff.push_back(_type.at(j));
  }

  // Adding a comma separator [23]
  frame_buff.push_back(44);

  // Add the data of the frame [?]
  for (i = 15, j = 0; j < strlen(data); i++, j++)
  {
    frame_buff.push_back(data[j]);
  }

  // Adding a comma separator [? + 1]
  frame_buff.push_back(44);

  // Add the checksum [? + 1 + 2]
  std::string str_checksum = intToString(checksum(frame_buff, frame_length - 2), 16);
  for (i = 0; i < str_checksum.size(); i++)
  {
    frame_buff.push_back(str_checksum.at(i));
  }

  // Add the end character
  frame_buff.push_back(END);

  return frame_buff;
}

std::vector<uint8_t> deviceGetId()
{
  String chipId;
  std::vector<uint8_t> id;
  int i = 0, j = 0;

  //The chip ID is essentially its MAC address(length: 6 bytes).
  chipId = WiFi.macAddress(); //ESP.getEfuseMac();

  std::string newChipId;

  // Remove colon
  newChipId = replace(std::string(chipId.c_str()), ":", "");

  //Serial.println(newChipId.c_str());

  // Converts the couples into a byte
  for (i = 0; i < 12; i++)
  {
    //long temp = strtol(ascii[i], NULL, 16);
    long temp = (int)newChipId.at(i);
    //Serial.println(temp);
    id.push_back(temp);
  }

  return id;
}

uint8_t checksum(std::vector<uint8_t> data, int size_data)
{
  uint16_t sum = 0xFFFF;

  for (uint8_t i = 0; i < size_data; i++)
  {
    sum -= data[i];
  }

  return sum & 0x00FF;
}

std::string replace(std::string word, std::string target, std::string replacement)
{
  int len, loop = 0;
  std::string nword = "", let;
  len = word.length();
  len--;
  while (loop <= len)
  {
    let = word.substr(loop, 1);
    if (let == target)
    {
      nword = nword + replacement;
    }
    else
    {
      nword = nword + let;
    }
    loop++;
  }
  return nword;
}

std::string intToString(int num, int base)
{
  String str = String(num, base);

  str.toUpperCase();

  return std::string(str.c_str());
}
