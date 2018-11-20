/* Project      :

   Program name :

   Author       : Gustavo L. P. da Silva

   Email        : GustavoLopes899@gmail.com

   Date         :

   Purpose      : Monitor the temperature and the electric current on devices with LM35 (temperature) and SCT-013 (electric current) sensors.
*/

#include <SPI.h>
#include <Ethernet.h>
#include <TimeLib.h>
#include <EmonLib.h>
#include <EthernetReset.h>
#include <Siren.h>
#include <NTP.h>
#include <avr/wdt.h>

#define TAM_S 11                // tam small //
#define TAM_L 85                // tam large //
#define INTERVAL 60000          // interval between the readings (in milliseconds)
#define CALIBRATION_TIMES 30    // number of readings to calibrate

//--------------- Pins ---------------//
PROGMEM const int tempPin = A3;                   // lm35 pin
PROGMEM const int pinSCT = A5;                    // Analog pin connected to SCT-013 sensor
PROGMEM const int relePin = 9;                    // relay module pin

//--------------- Auxiliar Variables ---------------//
PROGMEM const boolean serial = true;      // variable to enable/disable the serial output
float reading;                            // temperature reading variable
const float voltage_reference = 1.1;      // used to change the reference's voltage, could be changed depending of the board used
PROGMEM const uint8_t equipment = 2;      // equipment's number (1 = Laboratory; 2 - DataCenter; 3 - Meat Laboratory)

//--------------- Ethernet ---------------//
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xFD};    // mac address from arduino
byte ip[] = { 10, 156, 10, 13 };                      // ip address from arduino
EthernetClient client;                                // used to access insertion on webservice
EthernetClient client_status;                         // used to access the status information to turns on the siren
EthernetReset reset(8080);                            // object to a remote reset/reprogram on arduino
byte ip_webservice[] = { 200, 19, 231, 235 };         // ip address from the webservice

//--------------- Database Variables ---------------//
char actualHour[TAM_S] = "", actualMinute[TAM_S] = "", actualSecond[TAM_S] = "";
char actualMonth[TAM_S] = "", actualDay[TAM_S] = "";
char sentence_temperature[100];
char sentence_current[100];

//--------------- SCT-013 Sensor Variables ---------------//
EnergyMonitor SCT013;

//--------------- Sensor Index Variables ---------------//
PROGMEM const int index_SCT = 9;
PROGMEM const int sensor_index = 10;

void setup() {
  if (serial) {
    Serial.begin(9600);
  }
  analogReference(INTERNAL1V1);         // needed use a 1.1v voltage_reference to work properly on Arduino Mega
  pinMode(tempPin, INPUT);

  //--------------- Ethernet Inicialization ---------------//
  Ethernet.begin(mac, ip);

  //--------------- NTP/Date Inicialization ---------------//
  syncTimeNTP();
  getActualDate();

  //--------------- Energy Monitor Inicialization ---------------//
  //SCT013.current(pinSCT, 1.70101);
  SCT013.current(pinSCT, 6.06060606061);

  //--------------- Watchdog Inicialization ---------------//
  wdt_enable(WDTO_8S);

  //--------------- Calibration ---------------//
  Serial.print(F("Calibrating..."));
  for (int i = 0; i < CALIBRATION_TIMES; i++) {
    reading = (voltage_reference * analogRead(tempPin) * 100.0) / 1024;     // Calibrate the temperature's reading
    SCT013.calcIrms(1480);                                                  // Calibrate the eletric current's reading
    checkStatus();                                                          // Calibrate the initial status
    wdt_reset();
  }
  Serial.println(F(" complete"));
}

void loop() {
  char temperature[TAM_S];      // sentence from the temperature insertion on website
  char current[TAM_S];          // sentence from the electric current insertion on website
  double irms;                  // stores the eletric current's value
  char actualDate[TAM_S];       // stores the formatted date
  char actualTime[TAM_S];       // stores the formatted time

  reading = (voltage_reference * analogRead(tempPin) * 100.0) / 1024;     // Calculate the current temperature
  Serial.print(F("Temperature: "));
  Serial.println((float)reading);

  irms = SCT013.calcIrms(1480);
  Serial.print(F("Corrente = "));
  Serial.print(irms);
  Serial.println(F(" A"));
  Serial.println();

  // get actual time (hour, minute, second) and check if has any change in day, format to save on database //
  checkChangeDay();
  getActualTime();
  sprintf(actualDate, "%d-%s-%s", year(), actualMonth, actualDay);
  sprintf(actualTime, "%s:%s:%s", actualHour, actualMinute, actualSecond);

  // formatting char array to send a http request. URL format: http://tecnologias.cppse.embrapa.br/arduino/leitura_teste.php?sensor='1'&data='2018-08-26'&hora='10:23:51'&valor=18.36 //
  dtostrf(reading, 4, 2, temperature);      // float to char array
  sprintf(sentence_temperature, "GET /arduino/leitura_teste.php?sensor=%d&data='%s'&hora='%s'&valor=%s HTTP/1.1", sensor_index, actualDate, actualTime, temperature);
  dtostrf(irms, 4, 2, current);             // float to char array
  sprintf(sentence_current, "GET /arduino/leitura_teste.php?sensor=%d&data='%s'&hora='%s'&valor=%s HTTP/1.1", index_SCT, actualDate, actualTime, current);

  // save on database with web service //
  sendHttpRequest(sentence_temperature);
  sendHttpRequest(sentence_current);

  // check status on webservice //
  Serial.print(F(">> Status: "));
  switch (checkStatus()) {
    case CONSTANT: {
        sirenControl(relePin, CONSTANT);
        Serial.println(F("Out of range."));
        break;
      }
    case NONE: {
        sirenControl(relePin, NONE);
        Serial.println(F("Normal."));
        break;
      }
    case SPACED: {
        sirenControl(relePin, SPACED);
        Serial.println(F("DB Error."));
        break;
      }
    default: {
        sirenControl(relePin, NETWORK_ERROR);
        Serial.println(F("Network Error."));
        break;
      }
  }
  Serial.println();

  wdt_reset();

  // check reset client //
    for (uint8_t i = 0; i <= INTERVAL / 1000; i++) {      // maybe change INTERVAL value 60000ms to 60s
      // Check if the reset command was send
      reset.check();
      wdt_reset();
      delay(1000);
    }
  //delay(INTERVAL);                          // wait for x milliseconds before taking the reading again
}

// function to connect on web service address //
void connectWebService() {
  // client.connect(ip_webservice, 80);
  //client_status.connect(ip_webservice, 80);
  //Serial.print(F("Connecting with web service... "));
  if (!client.connected()) {
    if (client.connect(ip_webservice, 80)) {
      //Serial.println(F("Client connected."));
    } else {
      //Serial.println(F("Client connection failed."));
    }
  }
  if (!client_status.connected()) {
    if (client_status.connect(ip_webservice, 80)) {
      //Serial.println(F("Status client connected."));
    } else {
      //Serial.println(F("Status client connection failed."));
    }
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
  client.stop();
}

// function to check the status and return a code for them (0: normal state; 1: attention state (out of range); -1: database error) //
int checkStatus() {
  char alert[TAM_L];
  char val[3];
  char body[15] = "";
  uint8_t i = 0;
  int value = 2;
  char c, d;
  connectWebService();
  if (client_status.connected()) {
    // formatting char array to send a http request. URL format: http://tecnologias.cppse.embrapa.br/arduino/alerta.php?equipamento=2 //
    sprintf(alert, "GET /arduino/alerta.php?equipamento=%d HTTP/1.1", equipment);
    // Make a HTTP request //
    client_status.println(alert);
    client_status.println("Host: tecnologias.cppse.embrapa.br");
    client_status.println("Connection: keep-alive");
    client_status.println();
    while (client_status.available()) {
      //c = client_status.read();
      //Serial.print(c);

      if ((d = client_status.read()) == '<' && (c = client_status.read()) == 'b') {
        body[i++] = d;
        body[i++] = c;
        while ((c = client_status.read()) != '>') {
          body[i++] = c;
        }
        body[i++] = c;
        body[i] = '\0';
        i = 0;
      } else {
        if (body[0] == '<' && body[1] == 'b' && body[2] == 'o' && body[3] == 'd' && body[4] == 'y' && body[5] == '>') {
          if ((c = client_status.read()) == '-') {
            val[0] = c;
            if (isDigit(c = client_status.read())) {
              val[1] = c;
              val[2] = '\0';
            } else {
              break;
            }
          } else {
            if (isDigit(c)) {
              val[0] = c;
            } else {
              val[0] = '2';
            }
            val[1] = '\0';
          }
          value = atoi(val);
          break;
        }
      }

    }
  }
  /*Serial.print(F("Value: "));
  Serial.println(value);*/
  return value;
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
    getActualDate();
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
