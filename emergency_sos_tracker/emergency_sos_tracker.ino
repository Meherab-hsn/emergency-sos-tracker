#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>

#define SIM_RX 2
#define SIM_TX 3
#define GPS_RX 6
#define GPS_TX 5
#define BUZZER_PIN A0
#define BUTTON_PIN A1

SoftwareSerial simSerial(SIM_RX, SIM_TX);
SoftwareSerial gpsSerial(GPS_RX, GPS_TX);
TinyGPSPlus gps;
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {7, 8, 9, 10};
byte colPins[COLS] = {11, 12, 13, 4};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String numbers[3] = {"", "", ""};
String tempNumber = "";
bool inputMode = false;
int editingSlot = -1;

bool isTracking = false;
unsigned long lastTrackTime = 0;
const unsigned long TRACK_INTERVAL = 300000;

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.init();
  lcd.backlight();
  lcd.print("System Booting..");

  Serial.begin(9600);
  simSerial.begin(9600);
  gpsSerial.begin(9600);

  for(int i = 0; i < 3; i++) {
    numbers[i] = readFromEEPROM(i * 20);
  }

  delay(2000);

  simSerial.listen();
  simSerial.println("AT");
  delay(500);
  simSerial.println("AT+CMGF=1");
  delay(500);

  showStandby();
}

void loop() {
  gpsSerial.listen();
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  char key = keypad.getKey();
  if (key) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(50);
    digitalWrite(BUZZER_PIN, LOW);

    if (isTracking && key == '#') {
      isTracking = false;
      lcd.clear();
      lcd.print("Tracking Stopped");
      delay(2000);
      showStandby();
    }
    else if (!inputMode && !isTracking) {
      if (key == 'A') startInput(0);
      else if (key == 'B') startInput(1);
      else if (key == 'C') startInput(2);
    }
    else if (inputMode) {
      if (key == '#') {
        if(tempNumber.length() > 0) {
          numbers[editingSlot] = tempNumber;
          writeToEEPROM(editingSlot * 20, tempNumber);
          lcd.clear();
          lcd.print("Saved Num ");
          lcd.print(editingSlot + 1);
          delay(2000);
        }
        inputMode = false;
        showStandby();
      }
      else if (key == 'D') {
        if (tempNumber.length() > 0) {
          tempNumber.remove(tempNumber.length() - 1);
          lcd.setCursor(0, 1);
          lcd.print("                ");
          lcd.setCursor(0, 1);
          lcd.print(tempNumber);
        } else {
          numbers[editingSlot] = "";
          writeToEEPROM(editingSlot * 20, "");
          lcd.clear();
          lcd.print("Number Deleted!");
          delay(2000);
          inputMode = false;
          showStandby();
        }
      }
      else if (tempNumber.length() < 15 && key != '*' && key != 'A' && key != 'B' && key != 'C') {
        tempNumber += key;
        lcd.print(key);
      }
    }
  }

  if (!inputMode && digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    unsigned long pressStart = millis();
    bool sosTriggered = false;

    while(digitalRead(BUTTON_PIN) == LOW) {
      if (millis() - pressStart >= 2000 && !sosTriggered) {
        if (numbers[0] != "" || numbers[1] != "" || numbers[2] != "") {
          sosTriggered = true;
          triggerSOS();

          isTracking = true;
          lastTrackTime = millis();
          showTrackingStatus();
        } else {
          lcd.clear();
          lcd.print("No Nums Saved!");
          delay(2000);
          showStandby();
          sosTriggered = true;
        }
      }
    }
  }

  if (isTracking && (millis() - lastTrackTime >= TRACK_INTERVAL)) {
    sendLiveTrackingSMS();
    lastTrackTime = millis();
  }
}

void startInput(int slot) {
  editingSlot = slot;
  inputMode = true;
  tempNumber = "";
  lcd.clear();
  lcd.print("Set Num ");
  lcd.print(slot + 1);
  lcd.print(":");
  lcd.setCursor(0, 1);
}

void showStandby() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SOS Ready");
  lcd.setCursor(0, 1);
  int count = (numbers[0]!=""?1:0) + (numbers[1]!=""?1:0) + (numbers[2]!=""?1:0);
  lcd.print(count);
  lcd.print(" Nums Saved");
}

void showTrackingStatus() {
  lcd.clear();
  lcd.print("Live Track: ON");
  lcd.setCursor(0, 1);
  lcd.print("Press # to Stop");
}

void triggerSOS() {
  lcd.clear();
  lcd.print("SOS TRIGGERED!");

  digitalWrite(BUZZER_PIN, HIGH);
  delay(2000);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.clear();
  lcd.print("Sending SMS...");

  String locationLink = getGPSLocation();
  simSerial.listen();

  for(int i=0; i<3; i++) {
    if(numbers[i] != "") {
      simSerial.println("AT+CMGF=1");
      delay(500);
      simSerial.print("AT+CMGS=\"");
      simSerial.print(numbers[i]);
      simSerial.println("\"");
      delay(1000);
      simSerial.print("EMERGENCY SOS!\nLocation: ");
      simSerial.print(locationLink);
      delay(500);
      simSerial.write(26);
      delay(5000);
    }
  }

  lcd.clear();
  lcd.print("All SMS Sent!");
  delay(3000);
}

void sendLiveTrackingSMS() {
  if (numbers[0] == "") return;

  lcd.clear();
  lcd.print("Sending Live Loc");

  String locationLink = getGPSLocation();
  simSerial.listen();
  simSerial.println("AT+CMGF=1");
  delay(500);
  simSerial.print("AT+CMGS=\"");
  simSerial.print(numbers[0]);
  simSerial.println("\"");
  delay(1000);
  simSerial.print("LIVE TRACKING:\n");
  simSerial.print(locationLink);
  delay(500);
  simSerial.write(26);
  delay(5000);

  showTrackingStatus();
}

String getGPSLocation() {
  if (gps.location.isValid()) {
    String lat = String(gps.location.lat(), 6);
    String lng = String(gps.location.lng(), 6);
    return "https://maps.google.com/?q=" + lat + "," + lng;
  }
  return "GPS Signal Not Found";
}

void writeToEEPROM(int startAddr, String num) {
  EEPROM.write(startAddr, num.length());
  for (int i = 0; i < num.length(); i++) {
    EEPROM.write(startAddr + 1 + i, num[i]);
  }
}

String readFromEEPROM(int startAddr) {
  int len = EEPROM.read(startAddr);
  if (len == 255 || len == 0 || len > 15) return "";
  String num = "";
  for (int i = 0; i < len; i++) {
    num += char(EEPROM.read(startAddr + 1 + i));
  }
  return num;
}
