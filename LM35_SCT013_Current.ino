/* Project      :

   Program name :

   Author       : Gustavo L. P. da Silva

   Email        : GustavoLopes899@gmail.com

   Date         :

   Purpose      : Monitor the temperature and the electric current on devices with LM35 (temperature) and SCT-013 (electric current) sensors.
*/

#include <SPI.h>
#include <Ethernet.h>
#include <Time.h>
#include <TimeLib.h>
#include <HttpClient.h>
#include <EmonLib.h>

#define TAM_S 11                // tam small //
#define TAM_L 85                // tam large //
#define INTERVAL 60000          // interval between the readings (in milliseconds)
#define CALIBRATION_TIMES 30    // number of readings to calibrate

//--------------- Pins ---------------//
int tempPin = A3;                         // lm35 pin
int pinSCT = A5;                          // Analog pint connected to SCT-013 sensor
int resetPin = 12;                        // pin to reset the board

//--------------- Auxiliar ---------------//
float reading = 0;                        // temperature reading variable
const float voltage_reference = 1.1;      // used to change the reference's voltage, could be changed depending of the board used

//--------------- Ethernet ---------------//
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xFD};
IPAddress ip(10, 156, 10, 13);
EthernetServer server(80);
EthernetClient client;
IPAddress ip_webservice(200, 19, 231, 235);

//--------------- Database Variables ---------------//
char actualHour[TAM_S] = "", actualMinute[TAM_S] = "", actualSecond[TAM_S] = "";
char actualMonth[TAM_S] = "", actualDay[TAM_S] = "";
char sentence_temperature[100];
char sentence_current[100];

//--------------- NTP Server ---------------//
EthernetUDP Udp;
const int NTP_PACKET_SIZE = 48;         // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];     // buffer to hold incoming & outgoing packets
unsigned int localPort = 8888;          // local port to listen for UDP packets
IPAddress timeServer(200, 160, 7, 186); // ntp.br server ip address
const int timeZone = -3;                // UTC -3  BRT Brasília Time
const int interval_ntp = 86400;         // Number of seconds between re-syncs (86400s = 24hs)

//--------------- SCT-013 Sensor Variables ---------------//
EnergyMonitor SCT013;

//--------------- Sensor Index Variables ---------------//
const int index_SCT = 9;
const int sensor_index = 10;

void setup() {
  Serial.begin(9600);
  analogReference(INTERNAL1V1);
  pinMode(tempPin, INPUT);
  digitalWrite(resetPin, HIGH);
  Serial.begin(9600);

  //--------------- Ethernet Inicialization ---------------//
  Ethernet.begin(mac, ip);

  //--------------- NTP Inicialization ---------------//
  Udp.begin(localPort);
  Serial.println("Waiting for sync in NTP server...");
  while (year() < 2018) {
    setSyncProvider(getNtpTime);
    getActualDate();
  }

  //--------------- Energy Monitor Inicialization ---------------//
  //SCT013.current(pinSCT, 1.70101);
  SCT013.current(pinSCT, 6.06060606061);

  //--------------- Calibration ---------------//
  for (int i = 0; i < CALIBRATION_TIMES; i++) {
    reading = (voltage_reference * analogRead(tempPin) * 100.0) / 1024;     // Calibrate the temperature's reading
    double irms = SCT013.calcIrms(1480);                                    // Calibrate the eletric current's reading
  }
}

void loop() {
  char temperature[TAM_S];
  char current[TAM_S];

  reading = (voltage_reference * analogRead(tempPin) * 100.0) / 1024;     // Calculate the current temperature
  Serial.print("Temperature: ");
  Serial.println((float)reading);

  double irms = SCT013.calcIrms(1480);      // Calculate the eletric current's value
  Serial.print("Corrente = ");
  Serial.print(irms);
  Serial.println(" A");
  Serial.println();

  // get actual time (hour, minute, second) and check if has any change in day, format to save on database //
  checkChangeDay();
  getActualTime();
  char actualDate[TAM_S];
  sprintf(actualDate, "%d-%s-%s", year(), actualMonth, actualDay);
  char actualTime[TAM_S];
  sprintf(actualTime, "%s:%s:%s", actualHour, actualMinute, actualSecond);

  // formatting char array to send a http request. URL format: http://tecnologias.cppse.embrapa.br/arduino/leitura.php?sensor='1'&data='2018-08-26'&hora='10:23:51'&valor=18.36 //
  dtostrf(reading, 4, 2, temperature);      // float to char array
  sprintf(sentence_temperature, "GET /arduino/leitura.php?sensor=%d&data='%s'&hora='%s'&valor=%s HTTP/1.1", sensor_index, actualDate, actualTime, temperature);
  dtostrf(irms, 4, 2, current);             // float to char array
  sprintf(sentence_current, "GET /arduino/leitura.php?sensor=%d&data='%s'&hora='%s'&valor=%s HTTP/1.1", index_SCT, actualDate, actualTime, current);

  // save on database with web service //
  sendHttpRequest(sentence_temperature);
  sendHttpRequest(sentence_current);

  delay(INTERVAL);                          // wait for x milliseconds before taking the reading again
}

// function to connect on web service address //
void connectWebService() {
  //Serial.print("Connecting with web service... ");
  if (client.connect(ip_webservice, 80)) {
    //Serial.println("connected.");
  } else {
    //Serial.println("connection failed");
  }
}

// function to send a http request used to save on database //
void sendHttpRequest(char sentence[]) {
  connectWebService();
  if (client.connected()) {
    // Make a HTTP request //
    client.println(sentence);
    client.println("Host: tecnologias.cppse.embrapa.br");
    client.println("Connection: close");
    client.println();
  }
}

// function to get a formatted date to char array //
void getActualDate() {
  if (month() < 10) {
    sprintf(actualMonth, "0%d", month());
  } else {
    sprintf(actualMonth, "%d", month());
  }
  if (day() < 10) {
    sprintf(actualDay, "0%d", day());
  } else {
    sprintf(actualDay, "%d", day());
  }
}

// function to check changes on day and update, if necessary //
void checkChangeDay() {
  char checkNewDay[TAM_S];
  if (day() < 10) {
    sprintf(checkNewDay, "0%d", day());
  } else {
    sprintf(checkNewDay, "%d", day());
  }
  if (checkNewDay != actualDay) {
    digitalWrite(resetPin, LOW);
    //getActualDate();
  }
}

// function to get a formatted time to char array //
void getActualTime() {
  if (hour() < 10) {
    sprintf(actualHour, "0%d", hour());
  } else {
    sprintf(actualHour, "%d", hour());
  }
  if (minute() < 10) {
    sprintf(actualMinute, "0%d", minute());
  } else {
    sprintf(actualMinute, "%d", minute());
  }
  if (second() < 10) {
    sprintf(actualSecond, "0%d", second());
  } else {
    sprintf(actualSecond, "%d", second());
  }
}

// function to initialize NTP synchronization //
time_t getNtpTime() {
  while (Udp.parsePacket() > 0) ;               // discard any previously received packets
  Serial.println("Transmit NTP Request");
  sendNTPpacket(timeServer);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response OK");
      Serial.println();
      setSyncInterval(interval_ntp);
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :(");
  Serial.println();
  return 0;                                    // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address //
void sendNTPpacket(IPAddress & address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

