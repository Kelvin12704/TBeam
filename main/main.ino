/*
 * LilyGO T-Beam: DHT11 + LCD1602A + LoRaWAN/TTN
 * GPS and unused OLED/deep-sleep/button code removed.
 * DHT11 DATA: GPIO 13
 * LCD1602A I2C: 0x27 (change to 0x3F if required)
 * Payload: 4 bytes: signed temperature x10, humidity x10 (big-endian)
 */

#include "configuration.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <math.h>
#include "axp20x.h"

// Power management for the T-Beam's AXP192, when present.
AXP20X_Class axp;
bool axp192_found = false;

#define DHTPIN 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

static uint8_t txBuffer[4] = {0, 0, 0, 0};
static bool packetQueued = false;

void scanI2Cdevice() {
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      if (addr == AXP192_SLAVE_ADDRESS) axp192_found = true;
    }
  }
}

void axp192Init() {
  if (!axp192_found) {
    Serial.println("AXP192 not found; skipping PMU setup");
    return;
  }

  if (axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
    Serial.println("AXP192 initialization failed");
    return;
  }

  Serial.println("AXP192 initialized");
  axp.setPowerOutPut(AXP192_LDO2, AXP202_ON);  // LoRa radio
  axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
  axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
  axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
  axp.setDCDC1Voltage(3300);
}

bool trySend() {
  const float temperature = dht.readTemperature();
  const float humidity = dht.readHumidity();

  Serial.println("--- DHT11 measurement ---");
  if (isnan(temperature) || isnan(humidity) ||
      humidity < 0 || humidity > 100 ||
      temperature < -20 || temperature > 60) {
    Serial.println("ERROR: invalid DHT11 reading");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DHT11 leesfout");
    return false;
  }

  Serial.printf("Temperature: %.1f C\n", temperature);
  Serial.printf("Humidity: %.1f %%\n", humidity);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(temperature, 1);
  lcd.print((char)223);
  lcd.print("C");
  lcd.setCursor(0, 1);
  lcd.print("Vocht: ");
  lcd.print(humidity, 1);
  lcd.print("%");

  // Temperature x10 as signed 16-bit, then humidity x10 as unsigned 16-bit.
  const int16_t temp10 = (int16_t)lroundf(temperature * 10.0f);
  const uint16_t hum10 = (uint16_t)lroundf(humidity * 10.0f);
  txBuffer[0] = (uint8_t)((temp10 >> 8) & 0xFF);
  txBuffer[1] = (uint8_t)(temp10 & 0xFF);
  txBuffer[2] = (uint8_t)((hum10 >> 8) & 0xFF);
  txBuffer[3] = (uint8_t)(hum10 & 0xFF);

#if LORAWAN_CONFIRMED_EVERY > 0
  const bool confirmed = (ttn_get_count() % LORAWAN_CONFIRMED_EVERY == 0);
#else
  const bool confirmed = false;
#endif

  packetQueued = true;
  ttn_send(txBuffer, sizeof(txBuffer), LORAWAN_PORT, confirmed);
  Serial.println("DHT11 uplink queued");
  return true;
}

void callback(uint8_t message) {
  switch (message) {
    case EV_JOINING:
      Serial.println("TTN: joining...");
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("TTN joining...");
      break;
    case EV_JOINED:
      Serial.println("TTN: joined");
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("TTN verbonden");
      break;
    case EV_JOIN_FAILED:
      Serial.println("TTN: join failed");
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("TTN join fout");
      break;
    case EV_REJOIN_FAILED:
      Serial.println("TTN: rejoin failed");
      break;
    case EV_RESET:
      Serial.println("TTN: reset");
      break;
    case EV_LINK_DEAD:
      Serial.println("TTN: link dead");
      break;
    case EV_ACK:
      Serial.println("TTN: ACK received");
      break;
    case EV_PENDING:
      Serial.println("TTN: message pending");
      break;
    case EV_QUEUED:
      Serial.println("TTN: message queued");
      break;
    case EV_TXCOMPLETE:
      if (packetQueued) {
        Serial.println("TTN: TX/RX cycle complete; check Live Data for reception");
        packetQueued = false;
      }
      break;
    case EV_RESPONSE: {
      const size_t len = ttn_response_len();
      uint8_t data[len];
      ttn_response(data, len);
      Serial.print("TTN response:");
      for (size_t i = 0; i < len; ++i) {
        Serial.printf(" %02X", data[i]);
      }
      Serial.println();
      break;
    }
    default:
      break;
  }
}

void setup() {
#ifdef DEBUG_PORT
  DEBUG_PORT.begin(SERIAL_BAUD);
#endif

  Serial.println("Start: T-Beam");
  Wire.begin(I2C_SDA, I2C_SCL);
  scanI2Cdevice();
  axp192Init();

  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("LilyGO DHT11");
  lcd.setCursor(0, 1); lcd.print("TTN starten...");

  if (!ttn_setup()) {
    Serial.println("TTN radio setup failed");
    if (REQUIRE_RADIO) {
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("Radio fout");
      sleep_forever();
    }
    return;
  }

  ttn_register(callback);
  Serial.println("TTN: starting join...");
  ttn_join();
  ttn_adr(LORAWAN_ADR);
}

void loop() {
  ttn_loop();
  static uint32_t lastSend = 0;
  if (lastSend == 0 || millis() - lastSend >= SEND_INTERVAL) {
    if (trySend()) lastSend = millis();
    else delay(1000);
  }
}
