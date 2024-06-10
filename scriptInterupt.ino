#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
#define TINY_GSM_DEBUG Serial

#include "SparkFun_Weather_Meter_Kit_Arduino_Library.h"
#include <esp_adc_cal.h>
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "utilities.h"
#include <TinyGsmClient.h>
#include <Ticker.h>

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 300          /* Time ESP32 will go to sleep (in seconds) */

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

const char *server_url = "http://rn134ha.duckdns.org/api/webhook/weather_station_webhook";

// Pin definitions
#define BATTERY_PIN 35  // ADC pin for battery
#define SOLAR_PIN 34    // ADC pin for solar panel
#define ONE_WIRE_BUS 15
#define WIND_DIRECTION_PIN 36
#define WIND_SPEED_PIN 14
#define RAINFALL_PIN 32

volatile uint16_t rainCount = 0; // Variable to count rain bucket tips
Ticker debounceTimer;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
SFEWeatherMeterKit weatherMeterKit(WIND_DIRECTION_PIN, WIND_SPEED_PIN, RAINFALL_PIN);

void IRAM_ATTR rainISR() {
    debounceTimer.detach(); // Detach any existing timer
    debounceTimer.attach_ms(10, []() {
        if (digitalRead(RAINFALL_PIN) == LOW) {
            rainCount++;
        }
    });
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("Setup ..."));

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Normal startup");
    } else {
        Serial.println("Wake up from deep sleep");
    }

    // Init des calibration params
    SFEWeatherMeterKitCalibrationParams calibrationParams = weatherMeterKit.getCalibrationParams();

    calibrationParams.vaneADCValues[WMK_ANGLE_0_0] = 3143;
    calibrationParams.vaneADCValues[WMK_ANGLE_22_5] = 1624;
    calibrationParams.vaneADCValues[WMK_ANGLE_45_0] = 1845;
    calibrationParams.vaneADCValues[WMK_ANGLE_67_5] = 335;
    calibrationParams.vaneADCValues[WMK_ANGLE_90_0] = 372;
    calibrationParams.vaneADCValues[WMK_ANGLE_112_5] = 264;
    calibrationParams.vaneADCValues[WMK_ANGLE_135_0] = 738;
    calibrationParams.vaneADCValues[WMK_ANGLE_157_5] = 506;
    calibrationParams.vaneADCValues[WMK_ANGLE_180_0] = 1149;
    calibrationParams.vaneADCValues[WMK_ANGLE_202_5] = 979;
    calibrationParams.vaneADCValues[WMK_ANGLE_225_0] = 2520;
    calibrationParams.vaneADCValues[WMK_ANGLE_247_5] = 2397;
    calibrationParams.vaneADCValues[WMK_ANGLE_270_0] = 3780;
    calibrationParams.vaneADCValues[WMK_ANGLE_292_5] = 3309;
    calibrationParams.vaneADCValues[WMK_ANGLE_315_0] = 3548;
    calibrationParams.vaneADCValues[WMK_ANGLE_337_5] = 2810;

    calibrationParams.mmPerRainfallCount = 0.2794; // Taille godet // FIXME sert plus a rien du coup
    calibrationParams.minMillisPerRainfall = 2000; // Deux seconde de debouce

    calibrationParams.kphPerCountPerSec = 2.4;
    calibrationParams.windSpeedMeasurementPeriodMillis = 1000;

    weatherMeterKit.setCalibrationParams(calibrationParams);
    weatherMeterKit.begin();

    pinMode(RAINFALL_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RAINFALL_PIN), rainISR, FALLING);

    analogReadResolution(12);
    sensors.begin();

    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    pinMode(BOARD_POWERON_PIN, OUTPUT);
    digitalWrite(BOARD_POWERON_PIN, HIGH);
    pinMode(MODEM_RESET_PIN, OUTPUT);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    delay(100);
    digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
    delay(2600);
    digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.println(".");
        if (retry++ > 10) {
            digitalWrite(BOARD_PWRKEY_PIN, LOW);
            delay(100);
            digitalWrite(BOARD_PWRKEY_PIN, HIGH);
            delay(1000);
            digitalWrite(BOARD_PWRKEY_PIN, LOW);
            retry = 0;
        }
    }
    Serial.println();

    SimStatus sim = SIM_ERROR;
    while (sim != SIM_READY) {
        sim = modem.getSimStatus();
        switch (sim) {
        case SIM_READY:
            Serial.println("SIM card online");
            break;
        case SIM_LOCKED:
            Serial.println("The SIM card is locked. Please unlock the SIM card first.");
            break;
        default:
            break;
        }
        delay(1000);
    }

#ifndef TINY_GSM_MODEM_SIM7672
    if (!modem.setNetworkMode(MODEM_NETWORK_AUTO)) {
        Serial.println("Set network mode failed!");
    }
    String mode = modem.getNetworkModes();
    Serial.print("Current network mode : ");
    Serial.println(mode);
#endif

    int16_t sq;
    Serial.print("Wait for the modem to register with the network.");
    RegStatus status = REG_NO_RESULT;
    while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED) {
        status = modem.getRegistrationStatus();
        switch (status) {
        case REG_UNREGISTERED:
        case REG_SEARCHING:
            sq = modem.getSignalQuality();
            Serial.printf("[%lu] Signal Quality:%d", millis() / 1000, sq);
            delay(1000);
            break;
        case REG_DENIED:
            Serial.println("Network registration was rejected, please check if the APN is correct");
            return;
        case REG_OK_HOME:
            Serial.println("Online registration successful");
            break;
        case REG_OK_ROAMING:
            Serial.println("Network registration successful, currently in roaming mode");
            break;
        default:
            Serial.printf("Registration Status:%d\n", status);
            delay(1000);
            break;
        }
    }
    Serial.println();

    Serial.printf("Registration Status:%d\n", status);
    delay(1000);

    String ueInfo;
    if (modem.getSystemInformation(ueInfo)) {
        Serial.print("Inquiring UE system information:");
        Serial.println(ueInfo);
    }

    if (!modem.enableNetwork()) {
        Serial.println("Enable network failed!");
    }

    delay(2000);

    String ipAddress = modem.getLocalIP();
    Serial.print("Network IP:"); Serial.println(ipAddress);

    modem.https_begin();

    if (!modem.https_set_url(server_url)) {
        Serial.println("Failed to set the URL. Please check the validity of the URL!");
        return;
    }

    modem.https_add_header("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6");
    modem.https_add_header("Accept-Encoding", "gzip, deflate, br");
    modem.https_add_header("Content-Type", "application/json");
    modem.https_set_accept_type("application/json");
    modem.https_set_user_agent("TinyGSM/LilyGo-A76XX");
}

void loop() {
    sensors.requestTemperatures();
    float temperature = sensors.getTempCByIndex(0);

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    uint16_t batteryADC = analogRead(BATTERY_PIN);
    uint32_t batteryVoltage = esp_adc_cal_raw_to_voltage(batteryADC, &adc_chars) * 2;

    uint16_t solarADC = analogRead(SOLAR_PIN);
    uint32_t solarVoltage = esp_adc_cal_raw_to_voltage(solarADC, &adc_chars) * 2;

    float windDirection = weatherMeterKit.getWindDirection();
    float windSpeed = weatherMeterKit.getWindSpeed();
    float totalRainfall = rainCount * 0.2794;

    rainCount = 0;

    String post_body = "{\"temperature\":" + String(temperature, 2) +
                       ", \"battery_voltage\":" + String(batteryVoltage / 1000.0, 2) +
                       ", \"solar_charging_voltage\":" + String(solarVoltage / 1000.0, 2) +
                       ", \"wind_heading\":" + String(windDirection, 3) +
                       ", \"wind_speed\":" + String(windSpeed, 3) +
                       ", \"total_rainfall\":" + String(totalRainfall, 3) + "}";

    Serial.println(post_body);

    int httpCode = modem.https_post(post_body);
    if (httpCode != 200) {
        Serial.print("HTTP POST request failed! Error code = ");
        Serial.println(httpCode);
    } else {
        Serial.println("Data sent successfully!");
    }

    goToSleep();
}

void goToSleep() {
    Serial.println("Preparing to sleep");

    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, HIGH);
    gpio_hold_en((gpio_num_t)MODEM_DTR_PIN);

    digitalWrite(BOARD_POWERON_PIN, LOW);
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_32, 0);

    Serial.println("Going to sleep now");
    delay(1000);
    esp_deep_sleep_start();
}
