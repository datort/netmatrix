//################# DISPLAY CONNECTIONS ################
// LED Matrix Pin -> ESP8266 Pin
// Vcc            -> 3v  (3V on NodeMCU 3V3 on WEMOS)
// Gnd            -> Gnd (G on NodeMCU)
// DIN            -> D7  (Same Pin for WEMOS)
// CS             -> D4  (Same Pin for WEMOS)
// CLK            -> D5  (Same Pin for WEMOS)
//################# DISPLAY CONNECTIONS ################

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include <Max72xxPanel.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

const String WIFI_SSID = "yourSsid";
const String WIFI_PASS = "yourPassword";

const uint64_t IMAGES[] = {
  0x0000000000000000,
  0x000000ffff000000,
  0x0000ff0000ff0000,
  0x4242424242424242,
  0x8181818181818181,
  0x9981818181818199,
  0xbd818181818181bd,
  0xff818181818181ff,
  0x7f818181818181fe,
  0x3f018181818180fc,
  0x1f010181818080f8,
  0x0f010101808080f0,
  0x07010100008080e0
};

float maxDownload = 1.0;
float maxUpload = 1.0;
int lastChange = 0;

String scrollText;
String serialMessage;

Max72xxPanel matrix = Max72xxPanel(D4, 3, 1);
int scrollInterval = 40;
int spacer = 1;
int width  = 5 + spacer; // The font width is 5 pixels


// ######### SETUP ############
void setup() {
  Serial.begin(115200);
  matrix.setIntensity(1); // 0-15
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  
  scrollText = "WiFi connected";
  dotMatrixInitSequence();
}

// ######### MAIN LOOP ############
void loop() {
  if (Serial.available() > 0) {
    // read the incoming byte:
    serialMessage = Serial.readString();
    handleSerialMessage();
  }
  
  if (scrollText.length() > 0) {
    scrollMessage();
    scrollText = "";
  }

  trafficMonitor();
  lastChange++;

  if (lastChange > 250) {
    lastChange = 0;
    maxDownload = ceil(maxDownload / 1.5);
    maxUpload = ceil(maxUpload / 1.5);
    if (maxDownload < 1) maxDownload = 1.0;
    if (maxUpload < 1) maxUpload = 1.0;
    scrollText = "Lowering to " + String(maxDownload) + "Mbit down/" + String(maxUpload) + "Mbit up";
  }
}

void playImages(int numDisplay) {
  for (int i = 0; i < sizeof(IMAGES)/8; i++) {
    displayImage(IMAGES[i], numDisplay);
    delay(75);
  }
}

void displayImage(uint64_t image, int numDisplay) {
  int startX = numDisplay * 8;
  for (int i = 0; i < 8; i++) {
    byte row = (image >> i * 8) & 0xFF;
    for (int j = 0; j < 8; j++) {
      matrix.drawPixel(i + startX, j, bitRead(row, j));
    }
  }
  matrix.write();
}

void handleSerialMessage() {
  if (serialMessage.startsWith("st:")) {
    scrollText = serialMessage.substring(3);
  } else if(serialMessage.startsWith("kitt")) {
    dotMatrixInitSequence();
  } else if(serialMessage.startsWith("ra")) {
    lastChange = -1;
  } else {
    Serial.println("Cannot handle this command");
  }

  serialMessage = "";
}

void scrollMessage() {
  matrix.fillScreen(LOW);
  for (int i = 0 ; i < width * scrollText.length() + matrix.width() - spacer; i++) {
    int letter = i / width;
    int x = (matrix.width() - 1) - i % width;
    int y = (matrix.height() - 8) / 2; // center the text vertically
    while ( x + width - spacer >= 0 && letter >= 0 ) {
      if ( letter < scrollText.length() ) {
        matrix.drawChar(x, y, scrollText[letter], HIGH, LOW, 1); // HIGH LOW means foreground ON, background OFF, reverse these to invert the display!
      }
      letter--;
      x -= width;
    }
    matrix.write(); // Send bitmap to display
    delay(scrollInterval);
  }
}

void dotMatrixInitSequence() {
  for (int i = 0; i < 8; i++) { kittScanner(i); }
  for (int i = 6; i > -1; i--) { kittScanner(i); }

  matrix.fillScreen(LOW);
}

void kittScanner(int i) {
  matrix.fillScreen(LOW);
  int base = i * 3;
  
  matrix.drawPixel(base,3, HIGH);
  matrix.drawPixel(base,4, HIGH);
  matrix.drawPixel(base + 1,3, HIGH);
  matrix.drawPixel(base + 1,4, HIGH);
  matrix.drawPixel(base + 2,3, HIGH);
  matrix.drawPixel(base + 2,4, HIGH);
  matrix.write();
  delay(200);
}

void trafficMonitor() {
  if (lastChange == 0) playImages(1);
  
  if ((WiFi.status() == WL_CONNECTED)) {

    WiFiClient client;
    HTTPClient http;

    http.begin(client, "http://192.168.178.1:49000/igdupnp/control/WANCommonIFC1");
    http.addHeader("Content-Type", "content-type: text/xml;charset=\"utf-8\"");
    http.addHeader("SOAPAction", "\"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1#GetAddonInfos\"");

    int httpCode = http.POST("<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:GetAdd-onInfos xmlns:u=urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1 /></s:Body></s:Envelope>");
    if (httpCode > 0) {

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        const String& payload = http.getString();
        
        float download = getXmlParam(payload, "NewByteReceiveRate").toInt() / 125000.0;
        float upload = getXmlParam(payload, "NewByteSendRate").toInt() / 125000.0;

        drawTransferRates(download, upload);
      }
    } else {
      Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    scrollText = "WiFi disconnected";
  }

  delay(1000);
}

void drawTransferRates(float download, float upload) {
  int downloadStartX = 2;
  int uploadStartX = 18;
  int startY = 7;

  if (download > maxDownload) {
    maxDownload = ceil(download);
    lastChange = -1; //urgs
    scrollText = String(download) + " Mbit/s down";
    return;
  }

  if (upload > maxUpload) {
    maxUpload = ceil(upload);
    lastChange = -1; //urgs
    scrollText = String(upload) + " Mbit/s up";
  }
  
  int downloadToLid = ceil(download * 32 / maxDownload);
  int uploadToLid = ceil(upload * 32 / maxUpload);
  
  
  for (int i = 0; i < 32; i++) {
    matrix.drawPixel(downloadStartX, startY, i < downloadToLid ? HIGH : LOW);
    matrix.drawPixel(uploadStartX, startY, i < uploadToLid ? HIGH : LOW);
    matrix.write();
    
    if (i % 4 == 3) {
      downloadStartX = 2;
      uploadStartX = 18;
      startY--;
    } else {
      downloadStartX++;
      uploadStartX++;
    }

    delay(25);
  }
}

String getXmlParam(String xml, String param) {
  if (xml.indexOf("<"+param+">") > 0) {
     int CountChar = param.length();
     int indexStart = xml.indexOf("<"+param+">");
     int indexStop = xml.indexOf("</"+param+">");  
     return xml.substring(indexStart + CountChar + 2, indexStop);
  }
  
  Serial.println("not found");
}
