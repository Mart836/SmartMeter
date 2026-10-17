// =====================================================================
//  SMART PREPAID METER  -  ESP32 version (Fixed for 2N2222 Transistor)
//  + WiFi web dashboard (status / manual relay / top-up / reference model)
//  Board: ESP32 Dev Module (Keyestudio ESP32, 38-pin)
//  Libraries: "LiquidCrystal I2C" (Frank de Brabander), "Keypad" (Mark Stanley)
//             WiFi, WebServer, ESPmDNS - all bundled with the ESP32 core,
//             nothing extra to install.
//
//  Everything below "WEB DASHBOARD ADDITIONS" is new. Keypad, LCD, GSM,
//  relay fail-safe and balance logic are unchanged from the original
//  sketch; the web controls call the same functions the keypad does.
// =====================================================================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <esp_system.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "dashboard_page.h"
// Private settings (WiFi login, SMS phone number) live in secrets.h, which
// git ignores. Copy secrets.example.h to secrets.h and fill it in.
#include "secrets.h"

// ==========================================
// WIFI  -  set WIFI_SSID / WIFI_PASS in secrets.h before uploading
// ==========================================
// If the board can't join WiFi within ~8s it keeps running as a normal
// standalone meter (keypad/LCD/GSM all still work) - WiFi is optional,
// not a dependency for the fail-safe cut-off to work.
bool wifiUp = false;
WebServer server(80);

// ==========================================
// PIN DEFINITIONS  (UNO pin -> ESP32 GPIO)
// ==========================================
#define RELAY_PIN    19  // Base of 2N2222 transistor (via 330 ohm resistor)
#define BUZZER_PIN   18  // Passive piezo (+)
#define CURR_PIN     39  // SVN, pot wiper from ACS712
#define SENSE_LOAD1  34  // SW1 output via 27k/10k divider
#define SENSE_LOAD2  35  // SW2 output via 27k/10k divider
#define I2C_SDA      21
#define I2C_SCL      22
#define GSM_RX       16  // ESP32 RX2 <- SIM900 TXD
#define GSM_TX       17  // ESP32 TX2 -> SIM900 RXD

// ------------------------------------------
// RELAY MODE:
// Set to 1 because GPIO19 triggers the 2N2222 transistor:
// HIGH (3.3V) = Transistor ON -> Relay CLOSED (Power ON)
// LOW  (0.0V) = Transistor OFF -> Relay OPEN  (Power CUT)
// ------------------------------------------
#define RELAY_MODE   1
bool relayIsOn = false;

#define GSM_ENABLED  1   // set to 0 to only print SMS text to Serial Monitor
// PHONE_NUMBER for SMS alerts is set in secrets.h

// LCD I2C address is found automatically at start-up (0x27 or 0x3F)
LiquidCrystal_I2C* lcdPtr = nullptr;
#define lcd (*lcdPtr)

byte findLcdAddress() {
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0x27;
}

// ==========================================
// KEYPAD (standard 4x4 membrane keypad)
// ==========================================
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {32, 33, 25, 26};   // R1, R2, R3, R4
byte colPins[COLS] = {27, 14, 13, 4};    // C1, C2, C3, C4
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
// ENTER = '#', CLEAR = '*'

// ==========================================
// METER STATE & PREDICTION VARIABLES
// ==========================================
float currentBalance = 5.0;  // kWh
float dailyAvgUsage  = 2.5;  // kWh/day
float costPerKWh     = 2.0;  // NAD per kWh
float targetDays     = 30.0; // days

// kept up to date every loop() so the web /api/status handler can read them
float daysRemaining = 0;
float nadToTopUp     = 0;
String loadStatus    = "NO LOAD (Standby)";
bool highUsageOn  = false;
bool lightUsageOn = false;
float lastAmps  = 0;
float lastWatts = 0;

String tokenBuffer = "";
bool powerCutOff = false;
bool lowBalanceAlerted = false;
bool manualCut = false;          // web/manual "pause" switch, independent of powerCutOff

const float ACS_SENS_V_PER_A = 0.185;
const float ACS_DIVIDER      = 1.5;
float acsZeroVolts = 2.5;
const float SUPPLY_VOLTS = 12.0; // nominal load-side supply, for the logged watts figure

// running total of energy used, for the laptop logger (currentBalance only
// ever counts down, this counts up so training data has a monotonic usage trace)
float usedKWhTotal = 0;

void processToken();
bool processTokenString(const String &token, String &message);
void beepBuzzer(int times);
void sendSMS(String text);
float readVolts(int pin, float divider);
void setRelay(bool on);
void setupWebServer();
String statusJson();

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  // Relay OFF first at power-on (Fail-safe default)
  setRelay(false);

  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);

  esp_reset_reason_t r = esp_reset_reason();
  Serial.println();
  Serial.print("Reset reason: ");
  switch (r) {
    case ESP_RST_POWERON:  Serial.println("POWER ON"); break;
    case ESP_RST_BROWNOUT: Serial.println("BROWNOUT (Check supply voltage!)"); break;
    case ESP_RST_SW:       Serial.println("SOFTWARE"); break;
    case ESP_RST_PANIC:    Serial.println("CRASH"); break;
    case ESP_RST_EXT:      Serial.println("EN BUTTON"); break;
    default:               Serial.println((int)r); break;
  }

  analogReadResolution(12);       // 0..4095
  analogSetAttenuation(ADC_11db); // Up to ~3.1V

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SENSE_LOAD1, INPUT);    // Input-only pins
  pinMode(SENSE_LOAD2, INPUT);

  // Calibrate ACS712 zero point with the relay OFF
  delay(200);
  float sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += readVolts(CURR_PIN, ACS_DIVIDER);
    delay(2);
  }
  acsZeroVolts = sum / 50.0;

  noTone(BUZZER_PIN);

  Wire.begin(I2C_SDA, I2C_SCL);
  byte lcdAddr = findLcdAddress();
  Serial.print("LCD found at address 0x"); Serial.println(lcdAddr, HEX);
  lcdPtr = new LiquidCrystal_I2C(lcdAddr, 16, 2);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Smart Meter v1");
  lcd.setCursor(0, 1); lcd.print("Online & Ready");

  beepBuzzer(1);

#if GSM_ENABLED
  Serial2.println("AT");        delay(300);
  Serial2.println("AT+CMGF=1"); delay(300);
#endif

  // ---------------- WEB DASHBOARD ADDITIONS: connect WiFi ----------------
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 8000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    wifiUp = true;
    Serial.print("WiFi connected, IP: "); Serial.println(WiFi.localIP());
    if (MDNS.begin("smartmeter")) {
      Serial.println("Dashboard also reachable at http://smartmeter.local/");
    }
    setupWebServer();
    server.begin();
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi OK. Open:");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    delay(2500);
  } else {
    Serial.println("WiFi not connected - running standalone (keypad/LCD/GSM only).");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("No WiFi - OK,");
    lcd.setCursor(0, 1); lcd.print("standalone mode");
    delay(1200);
  }
  // -------------------------------------------------------------------

  Serial.println("==========================================");
  Serial.println("  SMART PREPAID METER (ESP32) - ACTIVE    ");
  Serial.print  ("  ACS712 zero: "); Serial.print(acsZeroVolts, 3); Serial.println(" V");
  Serial.println("==========================================");

  delay(1200);
  lcd.clear();

  // Balance > 0 at start -> energize relay
  setRelay(true);
}

void loop() {
  if (wifiUp) server.handleClient();

  // 1. DETECT LOADS (switches via 27k/10k dividers)
  highUsageOn  = (analogRead(SENSE_LOAD1) > 700) && !powerCutOff;
  lightUsageOn = (analogRead(SENSE_LOAD2) > 700) && !powerCutOff;

  // 2. CONSUMPTION RATE
  float consumptionRate;
  if (highUsageOn && lightUsageOn) { consumptionRate = 0.040;  loadStatus = "HEAVY (both LEDs)"; }
  else if (highUsageOn)            { consumptionRate = 0.020;  loadStatus = "HIGH-USAGE LED ON"; }
  else if (lightUsageOn)           { consumptionRate = 0.015;  loadStatus = "LIGHT-USAGE LED ON"; }
  else                             { consumptionRate = 0.0005; loadStatus = "NO LOAD (Standby)"; }

  if (!powerCutOff && currentBalance > 0) {
    currentBalance -= consumptionRate;
    usedKWhTotal    += consumptionRate;
  }
  if (currentBalance < 0) currentBalance = 0;

  float amps = (readVolts(CURR_PIN, ACS_DIVIDER) - acsZeroVolts) / ACS_SENS_V_PER_A;
  lastAmps  = amps;
  lastWatts = amps * SUPPLY_VOLTS;

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1500) {
    Serial.print("[STATUS] "); Serial.print(powerCutOff ? "POWER CUT (balance 0)" : loadStatus);
    Serial.print(" | I: ");  Serial.print(amps, 3);
    Serial.print(" A | Bal: "); Serial.print(currentBalance, 2);
    Serial.print(" kWh | Relay Pin: "); Serial.println(relayIsOn ? "HIGH (ON)" : "LOW (OFF)");

    // one line for meter_logger.py on the laptop (10 fields after "LOG,")
    float sw1V = readVolts(SENSE_LOAD1, 1.0);
    float sw2V = readVolts(SENSE_LOAD2, 1.0);
    Serial.print("LOG,");
    Serial.print(millis());             Serial.print(",");
    Serial.print(currentBalance, 4);    Serial.print(",");
    Serial.print(usedKWhTotal, 4);      Serial.print(",");
    Serial.print(loadStatus);           Serial.print(",");
    Serial.print(sw1V, 3);              Serial.print(",");
    Serial.print(sw2V, 3);              Serial.print(",");
    Serial.print(SUPPLY_VOLTS, 2);      Serial.print(",");
    Serial.print(lastAmps, 3);          Serial.print(",");
    Serial.print(lastWatts, 2);         Serial.print(",");
    Serial.println(relayIsOn ? 1 : 0);

    lastPrint = millis();
  }

  // 3. FAIL-SAFE CUT-OFF & LOW BALANCE ALERTS
  if (currentBalance <= 0 && !powerCutOff) {
    powerCutOff = true;
    setRelay(false);                // Cuts GPIO 19 to LOW -> Transistor turns OFF -> Loads OFF
    tone(BUZZER_PIN, 1200);         // Continuous alarm tone
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("BALANCE ZERO!");
    lcd.setCursor(0, 1); lcd.print("Power cut");
    sendSMS("EMERGENCY: Balance is 0.00 kWh. Power DISCONNECTED!");
  } else if (currentBalance > 0 && currentBalance < 2.0 && !lowBalanceAlerted) {
    beepBuzzer(2);
    sendSMS("LOW BALANCE: Less than 2.00 kWh remains. Please top up.");
    lowBalanceAlerted = true;
  }

  // 4. PREDICTION MODEL (fixed-rate estimate; the trained model lives in the
  //    dashboard's "reference model" section until it's fed real logged data)
  daysRemaining = currentBalance / dailyAvgUsage;
  nadToTopUp = (targetDays - daysRemaining) * dailyAvgUsage * costPerKWh;
  if (nadToTopUp < 0) nadToTopUp = 0;

  // 5. LCD
  lcd.setCursor(0, 0);
  lcd.print("Bal: "); lcd.print(currentBalance, 2); lcd.print(" kWh   ");
  lcd.setCursor(0, 1);
  if (powerCutOff) {
    lcd.print("CUT - Enter token");
  } else if ((millis() / 3000) % 2 == 0) {
    lcd.print("Days Left: "); lcd.print(daysRemaining, 1); lcd.print("   ");
  } else {
    lcd.print("Rec: "); lcd.print(nadToTopUp, 0); lcd.print(" NAD     ");
  }

  // 6. KEYPAD TOKEN ENTRY
  char key = keypad.getKey();
  if (key) {
    if (!powerCutOff) beepBuzzer(1);
    if (key == '#') {
      processToken();
    } else if (key == '*') {
      tokenBuffer = "";
      lcd.clear(); lcd.print("Cleared");
      delay(400);
    } else if (key >= '0' && key <= '9') {
      tokenBuffer += key;
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Token Entry:");
      lcd.setCursor(0, 1); lcd.print(tokenBuffer);
      delay(300);
    }
  }

  // 7. MAINTAIN FAIL-SAFE STATE EVERY LOOP
  //    manualCut is the web dashboard's pause switch: it can only ever turn
  //    the load OFF, never override the zero-balance cut-off back ON.
  setRelay(!powerCutOff && !manualCut);

  delay(150);
}

// ==========================================
// HELPERS
// ==========================================
void setRelay(bool on) {
  relayIsOn = on;
  pinMode(RELAY_PIN, OUTPUT);
#if RELAY_MODE == 1
  // Transistor circuit: HIGH (3.3V) turns 2N2222 ON; LOW (0V) turns 2N2222 OFF
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
#else
  // Active-Low direct module
  if (on) {
    digitalWrite(RELAY_PIN, LOW);
  } else {
    pinMode(RELAY_PIN, INPUT);
  }
#endif
}

float readVolts(int pin, float divider) {
  return analogReadMilliVolts(pin) / 1000.0 * divider;
}

// Shared by the keypad ('#') and the web /api/topup handler, so both paths
// validate the token and credit the balance identically.
bool processTokenString(const String &token, String &message) {
  if (token == "1245") {           // Test token = +10 kWh
    currentBalance += 10.0;
    powerCutOff = false;
    lowBalanceAlerted = false;
    noTone(BUZZER_PIN);
    manualCut = false;
    setRelay(true);                 // Restore power
    message = "Success: +10 kWh credited";
    sendSMS("TOP-UP SUCCESS: 10 kWh credited. Power restored.");
    return true;
  }
  message = "Invalid token";
  return false;
}

void processToken() {
  lcd.clear(); lcd.print("Verifying...");
  delay(1000);

  String message;
  bool ok = processTokenString(tokenBuffer, message);
  if (ok) {
    lcd.clear(); lcd.print("Success: +10kWh");
  } else {
    lcd.clear(); lcd.print("Invalid Token!");
    if (!powerCutOff) beepBuzzer(3);
  }
  delay(1500);
  tokenBuffer = "";
  lcd.clear();
}

void beepBuzzer(int times) {
  for (int i = 0; i < times; i++) {
    tone(BUZZER_PIN, 2000);
    delay(100);
    noTone(BUZZER_PIN);
    delay(100);
  }
}

void sendSMS(String text) {
  Serial.println();
  Serial.println(">>> [GSM OUTGOING SMS] >>>");
  Serial.print("TO: ");      Serial.println(PHONE_NUMBER);
  Serial.print("MESSAGE: "); Serial.println(text);
  Serial.println("--------------------------");

#if GSM_ENABLED
  Serial2.println("AT+CMGF=1");
  delay(200);
  Serial2.print("AT+CMGS=\"");
  Serial2.print(PHONE_NUMBER);
  Serial2.println("\"");
  delay(300);
  Serial2.print(text);
  delay(100);
  Serial2.write(26);        // Ctrl+Z sends the SMS
  delay(1000);
  while (Serial2.available()) Serial.write(Serial2.read());
#endif
}

// =====================================================================
// WEB DASHBOARD ADDITIONS - HTTP API + served page
// Every handler sets a permissive CORS header; the page is same-origin
// anyway (served by this device), it's just a safety net if you ever
// open the dashboard from a file on your laptop instead.
// =====================================================================
String statusJson() {
  String j = "{";
  j += "\"balance\":" + String(currentBalance, 4) + ",";
  j += "\"usedKWh\":" + String(usedKWhTotal, 4) + ",";
  j += "\"loadStatus\":\"" + loadStatus + "\",";
  j += "\"highUsageOn\":" + String(highUsageOn ? "true" : "false") + ",";
  j += "\"lightUsageOn\":" + String(lightUsageOn ? "true" : "false") + ",";
  j += "\"amps\":" + String(lastAmps, 3) + ",";
  j += "\"watts\":" + String(lastWatts, 2) + ",";
  j += "\"powerCutOff\":" + String(powerCutOff ? "true" : "false") + ",";
  j += "\"relayIsOn\":" + String(relayIsOn ? "true" : "false") + ",";
  j += "\"manualCut\":" + String(manualCut ? "true" : "false") + ",";
  j += "\"daysRemaining\":" + String(daysRemaining, 2) + ",";
  j += "\"nadToTopUp\":" + String(nadToTopUp, 2) + ",";
  j += "\"dailyAvgUsage\":" + String(dailyAvgUsage, 3) + ",";
  j += "\"costPerKWh\":" + String(costPerKWh, 2) + ",";
  j += "\"targetDays\":" + String(targetDays, 0) + ",";
  j += "\"uptimeMs\":" + String(millis());
  j += "}";
  return j;
}

void handleApiStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", statusJson());
}

void handleApiRelay() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("cut")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"missing 'cut'\"}");
    return;
  }
  bool wantCut = server.arg("cut") == "1";
  String message;
  bool ok = true;
  if (!wantCut && powerCutOff) {
    // Can't manually force the relay on when the fail-safe has it cut -
    // that would defeat the zero-balance protection. Top-up is the only way.
    ok = false;
    message = "Balance is zero - top up first";
  } else {
    manualCut = wantCut;
    message = wantCut ? "Load switched off" : "Load switched on";
  }
  String j = "{\"success\":" + String(ok ? "true" : "false") + ",\"message\":\"" + message + "\"," +
             "\"relayIsOn\":" + String(relayIsOn ? "true" : "false") + "}";
  server.send(200, "application/json", j);
}

void handleApiTopup() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"missing 'token'\"}");
    return;
  }
  String message;
  bool ok = processTokenString(server.arg("token"), message);
  String j = "{\"success\":" + String(ok ? "true" : "false") + ",\"message\":\"" + message + "\"," +
             "\"balance\":" + String(currentBalance, 4) + "}";
  server.send(200, "application/json", j);
}

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", DASHBOARD_HTML);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/relay", HTTP_POST, handleApiRelay);
  server.on("/api/topup", HTTP_POST, handleApiTopup);
  server.onNotFound([]() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(404, "text/plain", "Not found");
  });
}
