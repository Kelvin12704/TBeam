
/*
 * LilyGO TTGO T-Beam V1.1
 * DHT11 + LCD1602A + LoRaWAN OTAA
 * ChirpStack EU868
 *
 * DHT11 DATA: GPIO 13
 * LCD1602A I2C: 0x27
 *
 * Payload: 4 bytes
 * Temperature x10 (signed 16-bit, big-endian)
 * Humidity x10 (unsigned 16-bit, big-endian)
 */

#include "configuration.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <math.h>
#include "axp20x.h"

// Hardware
AXP20X_Class axp;
bool axp192_found = false;

#define DHTPIN 13
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// LoRaWAN
static uint8_t txBuffer[4] = {0, 0, 0, 0};
static bool packetQueued = false;
static bool joined = false;
static uint32_t lastSend = 0;

// --------------------------------------------------
// I2C SCAN
// --------------------------------------------------

void scanI2Cdevice() {
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);

    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x");

      if (addr < 16) Serial.print('0');

      Serial.println(addr, HEX);

      if (addr == AXP192_SLAVE_ADDRESS) {
        axp192_found = true;
      }
    }
  }
}

// --------------------------------------------------
// AXP192 POWER MANAGEMENT
// --------------------------------------------------

void axp192Init() {
  if (!axp192_found) {
    Serial.println("AXP192 not found");
    return;
  }

  if (axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
    Serial.println("AXP192 initialization failed");
    return;
  }

  Serial.println("AXP192 initialized");

  axp.setPowerOutPut(AXP192_LDO2, AXP202_ON);
  axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
  axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
  axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
  axp.setDCDC1Voltage(3300);
}

// --------------------------------------------------
// DHT11 AND UPLINK
// --------------------------------------------------

bool trySend() {

  const float temperature = dht.readTemperature();
  const float humidity = dht.readHumidity();

  Serial.println();
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

  // LCD
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

  // Encode temperature and humidity
  const int16_t temp10 =
      (int16_t)lroundf(temperature * 10.0f);

  const uint16_t hum10 =
      (uint16_t)lroundf(humidity * 10.0f);

  txBuffer[0] = (uint8_t)((temp10 >> 8) & 0xFF);
  txBuffer[1] = (uint8_t)(temp10 & 0xFF);

  txBuffer[2] = (uint8_t)((hum10 >> 8) & 0xFF);
  txBuffer[3] = (uint8_t)(hum10 & 0xFF);

#if LORAWAN_CONFIRMED_EVERY > 0
  const bool confirmed =
      (ttn_get_count() % LORAWAN_CONFIRMED_EVERY == 0);
#else
  const bool confirmed = false;
#endif

  packetQueued = true;

  ttn_send(
      txBuffer,
      sizeof(txBuffer),
      LORAWAN_PORT,
      confirmed
  );

  Serial.println("DHT11 uplink queued");

  return true;
}

// --------------------------------------------------
// LORAWAN CALLBACK
// --------------------------------------------------

void callback(uint8_t message) {

  switch (message) {

    case EV_JOINING:

      joined = false;

      Serial.println("ChirpStack: joining...");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Joining...");
      break;

    case EV_JOINED:

      joined = true;
      lastSend = 0;

      Serial.println("ChirpStack: JOINED!");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Verbonden!");
      lcd.setCursor(0, 1);
      lcd.print("ChirpStack");
      break;

    case EV_JOIN_FAILED:

      joined = false;

      Serial.println("ChirpStack: JOIN FAILED");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Join mislukt");
      break;

    case EV_REJOIN_FAILED:

      joined = false;

      Serial.println("ChirpStack: rejoin failed");
      break;

    case EV_RESET:

      joined = false;
      packetQueued = false;

      Serial.println("LoRaWAN reset");
      break;

    case EV_LINK_DEAD:

      joined = false;

      Serial.println("LoRaWAN link dead");
      break;

    case EV_ACK:

      Serial.println("ACK received");
      break;

    case EV_PENDING:

      Serial.println("Message pending");
      break;

    case EV_QUEUED:

      Serial.println("Message queued");
      break;

    case EV_TXCOMPLETE:

      if (packetQueued) {

        Serial.println("Uplink TX/RX complete");

        packetQueued = false;
      }
      break;

    case EV_RESPONSE: {

      const size_t len = ttn_response_len();
      uint8_t data[len];

      ttn_response(data, len);

      Serial.print("Response:");

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

// --------------------------------------------------
// SETUP
// --------------------------------------------------

void setup() {

#ifdef DEBUG_PORT
  DEBUG_PORT.begin(SERIAL_BAUD);
#endif

  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("LILYGO T-BEAM");
  Serial.println("ChirpStack EU868 OTAA");
  Serial.println("==============================");

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  scanI2Cdevice();
  axp192Init();

  // Sensors and LCD
  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("LilyGO DHT11");

  lcd.setCursor(0, 1);
  lcd.print("ChirpStack...");

  // LoRa radio
  Serial.println("Initializing LoRa radio...");

  if (!ttn_setup()) {

    Serial.println("ERROR: LoRa radio setup failed");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Radio fout");

    if (REQUIRE_RADIO) {
      sleep_forever();
    }

    return;
  }

  // Register callback before joining
  ttn_register(callback);

  // ADR
  ttn_adr(LORAWAN_ADR);

  // Start OTAA
  Serial.println("Starting OTAA join...");

  ttn_erase_prefs();
  ttn_join();
}

// --------------------------------------------------
// LOOP
// --------------------------------------------------

void loop() {

  // Process LoRaWAN events continuously
  ttn_loop();

  // Wait until OTAA succeeds
  if (!joined) {
    return;
  }

  // Send sensor data at configured interval
  if (lastSend == 0 ||
      millis() - lastSend >= SEND_INTERVAL) {

    if (trySend()) {

      lastSend = millis();

    } else {

      delay(1000);
    }
  }
}
