#include <Arduino.h>
#include <DHT.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

// ── Pin config ────────────────────────────────────────────────────────────────
#define DHTPIN        2
#define DHTTYPE       DHT11
#define ACS712_PIN    A0

// ACS712 sensitivity — change to match your module:
//   05B → 185.0 mV/A (±5A)
//   20B → 100.0 mV/A (±20A)  ← default
//   30B →  66.0 mV/A (±30A)
#define ACS712_SENSITIVITY  100.0f
#define ACS712_VREF         2500.0f   // mV at zero current (Vcc/2)
#define VRMS_ASSUMED        230.0f    // Tunisia mains voltage
#define ADC_SAMPLES         500

// ── Bluetooth ─────────────────────────────────────────────────────────────────
#define BT_RX  10
#define BT_TX  11
SoftwareSerial bt(BT_RX, BT_TX);

// ── EEPROM layout (10 bytes per record) ───────────────────────────────────────
// [0-1] int16_t temp × 10
// [2-3] int16_t hum  × 10
// [4-5] int16_t current_mA
// [6-7] int16_t power_W
// [8-9] int16_t co2_g_per_h × 10
#define EEPROM_COUNT_ADDR  0
#define EEPROM_DATA_START  2
#define RECORD_SIZE        10
#define MAX_RECORDS        ((EEPROM.length() - EEPROM_DATA_START) / RECORD_SIZE)

// CO2 factor: Tunisia grid ANME value
#define CO2_KG_PER_KWH  0.614f

DHT dht(DHTPIN, DHTTYPE);

// ── ACS712 RMS current ────────────────────────────────────────────────────────
float readCurrentRMS() {
  long sumSq = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    float mV    = (analogRead(ACS712_PIN) / 1023.0f) * 5000.0f;
    float delta = mV - ACS712_VREF;
    float mA    = (delta / ACS712_SENSITIVITY) * 1000.0f;
    sumSq += (long)(mA * mA);
  }
  return sqrt((float)sumSq / ADC_SAMPLES);
}

float calcCO2_g_per_h(float power_W) {
  return (power_W / 1000.0f) * CO2_KG_PER_KWH * 1000.0f;
}

bool inRange(float v, float lo, float hi) {
  return v >= lo && v <= hi;
}

// ── EEPROM helpers ────────────────────────────────────────────────────────────
uint16_t getRecordCount() {
  uint16_t c;
  EEPROM.get(EEPROM_COUNT_ADDR, c);
  if (c == 0xFFFF) c = 0;
  return c;
}

void saveRecord(float temp, float hum, float current_mA, float power_W, float co2_g_h) {
  uint16_t count = getRecordCount();
  int addr = EEPROM_DATA_START + (count % MAX_RECORDS) * RECORD_SIZE;
  EEPROM.put(addr,     (int16_t)(temp       * 10));
  EEPROM.put(addr + 2, (int16_t)(hum        * 10));
  EEPROM.put(addr + 4, (int16_t)(current_mA     ));
  EEPROM.put(addr + 6, (int16_t)(power_W        ));
  EEPROM.put(addr + 8, (int16_t)(co2_g_h    * 10));
  if (count < MAX_RECORDS) count++;
  EEPROM.put(EEPROM_COUNT_ADDR, count);
}

void dumpEEPROM() {
  uint16_t count = getRecordCount();
  uint16_t slots = min(count, (uint16_t)MAX_RECORDS);
  Serial.println(F("=== EEPROM DUMP ==="));
  Serial.println(F("index,temp_C,hum_pct,current_mA,power_W,co2_g_per_h"));
  for (uint16_t i = 0; i < slots; i++) {
    int addr = EEPROM_DATA_START + i * RECORD_SIZE;
    int16_t t, h, c, p, co;
    EEPROM.get(addr,     t);
    EEPROM.get(addr + 2, h);
    EEPROM.get(addr + 4, c);
    EEPROM.get(addr + 6, p);
    EEPROM.get(addr + 8, co);
    Serial.print(i);              Serial.print(',');
    Serial.print(t  / 10.0f, 1); Serial.print(',');
    Serial.print(h  / 10.0f, 1); Serial.print(',');
    Serial.print((float)c,   1); Serial.print(',');
    Serial.print((float)p,   1); Serial.print(',');
    Serial.println(co / 10.0f, 1);
  }
  Serial.println(F("=== END DUMP ==="));
}

void clearEEPROM() {
  uint16_t z = 0;
  EEPROM.put(EEPROM_COUNT_ADDR, z);
  Serial.println(F("EEPROM cleared."));
}

// ── Flush EEPROM buffer over Bluetooth on boot ────────────────────────────────
void flushBuffer() {
  uint16_t count = getRecordCount();
  if (count == 0) return;
  uint16_t slots = min(count, (uint16_t)MAX_RECORDS);
  bt.println(F("{\"event\":\"buffer_flush_start\"}"));
  for (uint16_t i = 0; i < slots; i++) {
    int addr = EEPROM_DATA_START + i * RECORD_SIZE;
    int16_t t, h, c, p, co;
    EEPROM.get(addr,     t);
    EEPROM.get(addr + 2, h);
    EEPROM.get(addr + 4, c);
    EEPROM.get(addr + 6, p);
    EEPROM.get(addr + 8, co);
    StaticJsonDocument<200> rec;
    rec["buffered"]    = true;
    rec["index"]       = i;
    rec["temp_C"]      = t  / 10.0f;
    rec["hum_pct"]     = h  / 10.0f;
    rec["current_mA"]  = (float)c;
    rec["power_W"]     = (float)p;
    rec["co2_g_per_h"] = co / 10.0f;
    String s;
    serializeJson(rec, s);
    bt.println(s);
    delay(50);
  }
  bt.println(F("{\"event\":\"buffer_flush_end\"}"));
  clearEEPROM();
}

// ── JSON payload builder ──────────────────────────────────────────────────────
String buildPayload(float temp, float hum, float current_mA,
                    float power_W, float co2_g_h, bool allValid) {
  StaticJsonDocument<320> doc;
  doc["device_id"] = "CAT_NODE_01";
  doc["site_id"]   = "CAT_PARENTHESE";
  doc["ts_ms"]     = millis();
  doc["version"]   = "2.1";

  JsonObject s = doc.createNestedObject("sensors");
  s["temperature_C"]  = serialized(String(temp,        1));
  s["humidity_pct"]   = serialized(String(hum,         1));
  s["current_mA"]     = serialized(String(current_mA,  1));
  s["power_W"]        = serialized(String(power_W,     1));
  s["voltage_V"]      = VRMS_ASSUMED;
  s["co2_g_per_h"]    = serialized(String(co2_g_h,     2));
  s["co2_factor_src"] = "ANME_TN_0.614kgCO2_per_kWh";

  JsonObject q = doc.createNestedObject("quality");
  q["all_valid"]     = allValid;
  q["temp_valid"]    = inRange(temp,       -10, 80);
  q["hum_valid"]     = inRange(hum,          0, 100);
  q["current_valid"] = inRange(current_mA,   0, 20000);
  q["power_valid"]   = inRange(power_W,      0, 5000);

  String out;
  serializeJson(doc, out);
  return out;
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  bt.begin(9600);
  dht.begin();
  pinMode(ACS712_PIN, INPUT);

  Serial.print(F("Max EEPROM records: "));
  Serial.println(MAX_RECORDS);
  Serial.println(F("Commands: d=dump  c=clear  s=status"));

  delay(2000); // let DHT11 stabilize

  if (getRecordCount() > 0) {
    Serial.println(F("[BOOT] Flushing buffered records over BT..."));
    flushBuffer();
  } else {
    bt.println(F("{\"event\":\"boot\",\"buffered\":false}"));
  }
}

// ── loop ──────────────────────────────────────────────────────────────────────
void loop() {
  // Serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'd') dumpEEPROM();
    if (cmd == 'c') clearEEPROM();
    if (cmd == 's') {
      Serial.print(F("Buffered records: "));
      Serial.println(getRecordCount());
    }
  }

  // Bluetooth commands
  if (bt.available()) {
    char cmd = bt.read();
    if (cmd == 'd') dumpEEPROM();
    if (cmd == 'c') clearEEPROM();
  }

  // Read DHT11
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println(F("[WARN] DHT read failed"));
    delay(2000);
    return;
  }

  // Read ACS712
  float current_mA = readCurrentRMS();
  float power_W    = (current_mA / 1000.0f) * VRMS_ASSUMED;
  float co2_g_h    = calcCO2_g_per_h(power_W);

  bool allValid = inRange(temp,       -10, 80)
               && inRange(hum,          0, 100)
               && inRange(current_mA,   0, 20000)
               && inRange(power_W,      0, 5000);

  // Build and send JSON
  String payload = buildPayload(temp, hum, current_mA, power_W, co2_g_h, allValid);
  Serial.println(payload);
  bt.println(payload);

  // Save to EEPROM as offline buffer
  saveRecord(temp, hum, current_mA, power_W, co2_g_h);

  delay(5000);
}