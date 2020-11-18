#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include "arduino_secrets.h"

//WIFI
const char* ssid = SECRET_SSID;
const char* password = SECRET_WIFI_PASSWORD;

//MQTT
#define mqtt_server "192.168.88.99"
#define mqtt_user ""
#define mqtt_password ""
#define base_topic "sensor/heater/"

WiFiClient wifiClient;
PubSubClient client(wifiClient);

//Kamstrup setup
// Kamstrup Multical 402
word const kregnums[] = { 0x003C,0x0050,0x0056,0x0057,0x0059,0x004a,0x0044 };                                                   // The registers we want to get out of the meter
char* kregstrings[]   = { "energy","power","temperature_t1","temperature_t2","temperature_diff", "flow", "volume" }; // The name of the registers we want to get out of the meter in the same order as above
#define NUMREGS 7                                                                                                               // Number of registers above
#define KAMBAUD 1200

/*kamstrup_402_params = {
	"energy"	    : 0x3C,
	"power"         : 0x50,
	"temp1"         : 0x56,
	"temp2"         : 0x57,
	"tempdiff"      : 0x59,
	"flow"          : 0x4A,
	"volume"        : 0x44,
	"minflow_m"     : 0x8D,
	"maxflow_m"     : 0x8B,
	"minflowDate_m" : 0x8C,
	"maxflowDate_m" : 0x8A,
	"minpower_m"    : 0x91,
	"maxpower_m"    : 0x8F,
	"avgtemp1_m"    : 0x95,
	"avgtemp2_m"    : 0x96,
	"minpowerdate_m": 0x90,
	"maxpowerdate_m": 0x8E,
	"minflow_y"     : 0x7E,
	"maxflow_y"     : 0x7C,
	"minflowdate_y" : 0x7D,
	"maxflowdate_y" : 0x7B,
	"minpower_y"    : 0x82,
	"maxpower_y"    : 0x80,
	"avgtemp1_y"    : 0x92,
	"avgtemp2_y"    : 0x93,
	"minpowerdate_y": 0x81,
	"maxpowerdate_y": 0x7F,
	"temp1xm3"      : 0x61,
	"temp2xm3"      : 0x6E,
	"infoevent"     : 0x71,
	"hourcounter"   : 0x3EC,
}*/

// Units
char*  units[65] = {"","Wh","kWh","MWh","GWh","j","kj","Mj",
  "Gj","Cal","kCal","Mcal","Gcal","varh","kvarh","Mvarh","Gvarh",
        "VAh","kVAh","MVAh","GVAh","kW","kW","MW","GW","kvar","kvar","Mvar",
        "Gvar","VA","kVA","MVA","GVA","V","A","kV","kA","C","K","l","m3",
        "l/h","m3/h","m3xC","ton","ton/h","h","hh:mm:ss","yy:mm:dd","yyyy:mm:dd",
        "mm:dd","","bar","RTC","ASCII","m3 x 10","ton xr 10","GJ x 10","minutes","Bitfield",
        "s","ms","days","RTC-Q","Datetime"};

// Pin definitions
#define PIN_KAMSER_RX  D5  // Kamstrup IR interface RX
#define PIN_KAMSER_TX  D7  // Kamstrup IR interface TX
#define PIN_LED        13  // Standard Arduino LED


// Kamstrup optical IR serial
#define KAMTIMEOUT 800  // Kamstrup timeout after transmit
SoftwareSerial kamSer(PIN_KAMSER_RX, PIN_KAMSER_TX, false);  // Initialize serial

void setup () {
  pinMode(D3, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D8, OUTPUT);
  digitalWrite(D3, HIGH);
  digitalWrite(D4, HIGH);
  digitalWrite(D8, LOW);
  
  Serial.begin(115200);
  Serial.println("BOOT");
  
  if (connectWIFI()) {
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  client.setServer(mqtt_server,1883);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, 0);
  
  // setup kamstrup serial
  pinMode(PIN_KAMSER_RX,INPUT);
  pinMode(PIN_KAMSER_TX,OUTPUT);
  
  kamSer.begin(KAMBAUD);
  
  delay(200);
  
 for (int kreg = 0; kreg < NUMREGS; kreg++) {
      kamReadReg(kreg);
      delay(100);
  }
  
   Serial.println("BOOT done.");
}

boolean connectWIFI()
{
  Serial.println("Connecting to WIFI");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.hostname("Heater MQTT Sensor");
  int count = 0;
  while (count < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      return (true);
    }
    delay(500);
    count++;
  }
  Serial.println("Wifi Connection timed out.");
  return false;
}
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("ESP8266Client")) {
    if (client.connect("ESP8266 Heater Sensor", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop () {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  for (int kreg = 0; kreg < NUMREGS; kreg++) {
    kamReadReg(kreg);
    delay(100);
  }
  
  digitalWrite(PIN_LED, digitalRead(PIN_KAMSER_RX));
  delay(2000);
}

float kamReadReg(unsigned short kreg) {

  byte recvmsg[40];  // buffer of bytes to hold the received data
  float rval;        // this will hold the final value

  // prepare message to send and send it
  byte sendmsg[] = { 0x3f, 0x10, 0x01, (kregnums[kreg] >> 8), (kregnums[kreg] & 0xff) };
  kamSend(sendmsg, 5);

  // listen if we get an answer
  unsigned short rxnum = kamReceive(recvmsg);

  // check if number of received bytes > 0 
  if(rxnum != 0){
    
    // decode the received message
    rval = kamDecode(kreg,recvmsg);
    
    // print out received value to terminal (debug)
    Serial.print(kregstrings[kreg]);
    Serial.print(": ");
    Serial.print(rval);
    Serial.print(" ");
    Serial.println();

    char result[100];   // array to hold the result.

    strcpy(result,base_topic); // copy string one into the result.
    strcat(result,kregstrings[kreg]);
    
    //Serial.println(result);
    client.publish(result,String(rval).c_str(),true);
    
    return rval;
  }
}

// kamSend - send data to Kamstrup meter
void kamSend(byte const *msg, int msgsize) {

  // append checksum bytes to message
  byte newmsg[msgsize+2];
  for (int i = 0; i < msgsize; i++) { newmsg[i] = msg[i]; }
  newmsg[msgsize++] = 0x00;
  newmsg[msgsize++] = 0x00;
  int c = crc_1021(newmsg, msgsize);
  newmsg[msgsize-2] = (c >> 8);
  newmsg[msgsize-1] = c & 0xff;

  // build final transmit message - escape various bytes
  byte txmsg[20] = { 0x80 };   // prefix
  int txsize = 1;
  for (int i = 0; i < msgsize; i++) {
    if (newmsg[i] == 0x06 or newmsg[i] == 0x0d or newmsg[i] == 0x1b or newmsg[i] == 0x40 or newmsg[i] == 0x80) {
      txmsg[txsize++] = 0x1b;
      txmsg[txsize++] = newmsg[i] ^ 0xff;
    } else {
      txmsg[txsize++] = newmsg[i];
    }
  }
  txmsg[txsize++] = 0x0d;  // EOF

  // send to serial interface
  for (int x = 0; x < txsize; x++) {
    kamSer.write(txmsg[x]);
  }

}

// kamReceive - receive bytes from Kamstrup meter
unsigned short kamReceive(byte recvmsg[]) {

  byte rxdata[50];  // buffer to hold received data
  unsigned long rxindex = 0;
  unsigned long starttime = millis();
  
  kamSer.flush();  // flush serial buffer - might contain noise

  byte r;
  
  // loop until EOL received or timeout
  while(r != 0x0d){
    
    // handle rx timeout
    if(millis()-starttime > KAMTIMEOUT) {
      Serial.println("Timed out listening for data");
      return 0;
    }

    // handle incoming data
    if (kamSer.available()) {

      // receive byte
      r = kamSer.read();
      if(r != 0x40) {  // don't append if we see the start marker
        // append data
        rxdata[rxindex] = r;
        rxindex++; 
      }

    }
  }

  // remove escape markers from received data
  unsigned short j = 0;
  for (unsigned short i = 0; i < rxindex -1; i++) {
    if (rxdata[i] == 0x1b) {
      byte v = rxdata[i+1] ^ 0xff;
      if (v != 0x06 and v != 0x0d and v != 0x1b and v != 0x40 and v != 0x80){
        Serial.print("Missing escape ");
        Serial.println(v,HEX);
      }
      recvmsg[j] = v;
      i++; // skip
    } else {
      recvmsg[j] = rxdata[i];
    }
    j++;
  }
  
  // check CRC
  if (crc_1021(recvmsg,j)) {
    Serial.println("CRC error: ");
    return 0;
  }
  
  return j;
  
}

// kamDecode - decodes received data
float kamDecode(unsigned short const kreg, byte const *msg) {

  // skip if message is not valid
  if (msg[0] != 0x3f or msg[1] != 0x10) {
    return false;
  }
  if (msg[2] != (kregnums[kreg] >> 8) or msg[3] != (kregnums[kreg] & 0xff)) {
    return false;
  }
    
  // decode the mantissa
  long x = 0;
  for (int i = 0; i < msg[5]; i++) {
    x <<= 8;
    x |= msg[i + 7];
  }
  
  // decode the exponent
  int i = msg[6] & 0x3f;
  if (msg[6] & 0x40) {
    i = -i;
  };
  float ifl = pow(10,i);
  if (msg[6] & 0x80) {
    ifl = -ifl;
  }

  // return final value
  return (float )(x * ifl);

}

// crc_1021 - calculate crc16
long crc_1021(byte const *inmsg, unsigned int len){
  long creg = 0x0000;
  for(unsigned int i = 0; i < len; i++) {
    int mask = 0x80;
    while(mask > 0) {
      creg <<= 1;
      if (inmsg[i] & mask){
        creg |= 1;
      }
      mask>>=1;
      if (creg & 0x10000) {
        creg &= 0xffff;
        creg ^= 0x1021;
      }
    }
  }
  return creg;
}
