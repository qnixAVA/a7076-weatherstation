#define TINY_GSM_RX_BUFFER 1024
#define TINY_GSM_DEBUG Serial
#define DUMP_AT_COMMANDS

#include "SparkFun_Weather_Meter_Kit_Arduino_Library.h"
#include <esp_adc_cal.h>
#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>
#include <DHT.h>

// === CONFIGURATION ===
#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP 300            // 5 minutes entre envois normaux
#define RAIN_AWAKE_MS 360000         // 6 minutes max en mode pluie (360s)
#define RAIN_MAX_EXTENSIONS 4        // 4 envois supplementaires max en mode pluie
#define BATTERY_CRITICAL_V 3.45      // Seuil critique : pas d'envoi si sous ce voltage
#define BATTERY_LOW_V 3.60           // Seuil faible : intervalle allonge

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

const char *server_url = "http://rn134ha.duckdns.org/api/webhook/weather_station_webhook";

#define BATTERY_PIN 35
#define SOLAR_PIN 34
#define WIND_DIRECTION_PIN 36
#define WIND_SPEED_PIN 14
#define RAINFALL_PIN 32
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Variables RTC (persistent apres deep sleep)
RTC_DATA_ATTR volatile int rainCount = 0;
RTC_DATA_ATTR volatile int cumulativeRainCount = 0;
RTC_DATA_ATTR int lastSentRainCount = 0;
RTC_DATA_ATTR bool wokeFromRain = false;
RTC_DATA_ATTR volatile uint64_t lastRainInterrupt = 0;

SFEWeatherMeterKit weatherMeterKit(WIND_DIRECTION_PIN, WIND_SPEED_PIN, RAINFALL_PIN);

// === ISR POUR PLUVIOMETRE ===
void IRAM_ATTR rainISR() {
    uint64_t now = esp_timer_get_time();
    if (now - lastRainInterrupt > 1000000) {  // 1s debounce
        rainCount++;
        cumulativeRainCount++;
        lastRainInterrupt = now;
    }
}

// === ENVOI HTTP VIA COMMANDES AT BRUTES ===
int sendHttpPost(const char *url, const String &body) {
    // Forcer la fermeture de toute session HTTP precedente
    modem.sendAT(GF("+HTTPTERM"));
    modem.waitResponse(1000L);
    delay(500);

    // HTTPINIT avec retry (3 tentatives)
    bool initOk = false;
    for (int i = 0; i < 3; i++) {
        modem.sendAT(GF("+HTTPINIT"));
        if (modem.waitResponse(5000L) == 1) {
            initOk = true;
            break;
        }
        Serial.print("HTTPINIT retry ");
        Serial.println(i + 1);
        modem.sendAT(GF("+HTTPTERM"));
        modem.waitResponse(1000L);
        delay(2000);
    }
    if (!initOk) {
        Serial.println("HTTPINIT failed apres 3 essais");
        return -1;
    }

    modem.sendAT(GF("+HTTPPARA=\"URL\",\""), url, GF("\""));
    if (modem.waitResponse(5000L) != 1) {
        Serial.println("HTTPPARA URL failed");
        return -2;
    }

    modem.sendAT(GF("+HTTPPARA=\"CONTENT\",\"application/json\""));
    if (modem.waitResponse(5000L) != 1) {
        Serial.println("HTTPPARA CONTENT failed");
        return -3;
    }

    modem.sendAT(GF("+HTTPDATA="), body.length(), GF(",10000"));
    if (modem.waitResponse(10000L, GF("DOWNLOAD")) != 1) {
        Serial.println("HTTPDATA not ready");
        return -4;
    }

    modem.stream.print(body);
    modem.stream.flush();
    if (modem.waitResponse(10000L) != 1) {
        Serial.println("HTTPDATA body failed");
        return -5;
    }

    modem.sendAT(GF("+HTTPACTION=1"));
    if (modem.waitResponse(5000L) != 1) {
        Serial.println("HTTPACTION failed");
        return -6;
    }

    int httpCode = -1;
    if (modem.waitResponse(30000L, GF("+HTTPACTION:")) == 1) {
        modem.stream.parseInt();
        httpCode = modem.stream.parseInt();
        int len = modem.stream.parseInt();
        Serial.print("HTTP code: "); Serial.print(httpCode);
        Serial.print(" / length: "); Serial.println(len);
    }

    modem.sendAT(GF("+HTTPTERM"));
    modem.waitResponse(1000L);

    return httpCode;
}

// === SETUP ===
void setup() {
    Serial.begin(115200);
    Serial.println(F("Setup ..."));

    // Liberer le GPIO hold du cycle precedent
    gpio_hold_dis((gpio_num_t)MODEM_DTR_PIN);

    // Activation pin 12 pour lecture batterie
    pinMode(12, OUTPUT);
    digitalWrite(12, HIGH);
    delay(100);

    // 1. Verification de la cause du reveil AVANT d'attacher l'ISR
    // Cela evite les conflits si le reed switch rebondit au boot
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Reveil par pluie detecte");
        rainCount++;
        cumulativeRainCount++;
        wokeFromRain = true;
        lastRainInterrupt = esp_timer_get_time();  // Activer le debounce immediatement
    }

    // 2. Attendre que le reed switch se stabilise avant d'attacher l'ISR
    delay(500);

    // 3. Configurer le pin pluie (ISR attachee APRES weatherMeterKit.begin())
    pinMode(RAINFALL_PIN, INPUT_PULLUP);

    // Calibration girouette SparkFun (valeurs ADC pour ESP32)
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
    calibrationParams.mmPerRainfallCount = 0.2794;
    calibrationParams.minMillisPerRainfall = 2000;
    calibrationParams.kphPerCountPerSec = 2;
    calibrationParams.windSpeedMeasurementPeriodMillis = 1000;

    weatherMeterKit.setCalibrationParams(calibrationParams);
    weatherMeterKit.begin();

    // Ecraser l'ISR de la librairie SparkFun par notre ISR custom
    // Stabiliser le pin avant d'attacher pour eviter un declenchement parasite
    delay(100);
    digitalRead(RAINFALL_PIN);  // Vider tout etat pendand
    attachInterrupt(digitalPinToInterrupt(RAINFALL_PIN), rainISR, FALLING);

    analogReadResolution(12);

    // Initialisation modem
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

    // Test AT
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

    // Attente SIM (timeout 15s)
    unsigned long simStart = millis();
    SimStatus sim = SIM_ERROR;
    while (sim != SIM_READY) {
        if (millis() - simStart > 15000) {
            Serial.println("SIM timeout 15s - retour sommeil");
            goToSleep();
        }
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
        delay(600);
    }

#ifndef TINY_GSM_MODEM_SIM7672
    if (!modem.setNetworkMode(MODEM_NETWORK_AUTO)) {
        Serial.println("Set network mode failed!");
    }
    String mode = modem.getNetworkModes();
    Serial.print("Current network mode : ");
    Serial.println(mode);
#endif

    // Attente reseau (timeout 45s)
    int16_t sq;
    Serial.print("Wait for the modem to register with the network.");
    unsigned long netStart = millis();
    RegStatus status = REG_NO_RESULT;
    while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED) {
        if (millis() - netStart > 45000) {
            Serial.println("Reseau timeout 45s - retour sommeil");
            goToSleep();
        }
        status = modem.getRegistrationStatus();
        switch (status) {
        case REG_UNREGISTERED:
        case REG_SEARCHING:
            sq = modem.getSignalQuality();
            Serial.printf("[%lu] Signal Quality:%d", millis() / 1000, sq);
            delay(600);
            break;
        case REG_DENIED:
            Serial.println("Network registration was rejected, please check if the APN is correct");
            goToSleep();
        case REG_OK_HOME:
            Serial.println("Online registration successful");
            break;
        case REG_OK_ROAMING:
            Serial.println("Network registration successful, currently in roaming mode");
            break;
        default:
            Serial.printf("Registration Status:%d\n", status);
            delay(600);
            break;
        }
    }
    Serial.println();

    Serial.printf("Registration Status:%d\n", status);
    delay(500);

    String ueInfo;
    if (modem.getSystemInformation(ueInfo)) {
        Serial.print("Inquiring UE system information:");
        Serial.println(ueInfo);
    }

    if (!modem.enableNetwork()) {
        Serial.println("Enable network failed!");
    }

    delay(1000);

    String ipAddress = modem.getLocalIP();
    Serial.print("Network IP:"); Serial.println(ipAddress);

    dht.begin();
    delay(2000);  // Attendre stabilisation DHT22 apres power-on
}

// === LECTURE ET ENVOI DES DONNEES ===
void readAndSendData() {
    // Lecture DHT22 avec retry
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    // Si la premiere lecture echoue (nan ou 0.0), on reessaie apres 2.5s
    if (isnan(temperature) || isnan(humidity) || temperature == 0.0 || humidity == 0.0) {
        Serial.println("DHT22 premiere lecture invalide, retry dans 2.5s...");
        delay(2500);
        temperature = dht.readTemperature();
        humidity = dht.readHumidity();
    }

    // Log pour debug
    Serial.print("DHT22 - Temp: "); Serial.print(temperature);
    Serial.print(" / Hum: "); Serial.println(humidity);

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    // Lecture batterie
    uint16_t batteryADC = analogRead(BATTERY_PIN);
    uint32_t batteryVoltage = esp_adc_cal_raw_to_voltage(batteryADC, &adc_chars) * 2;
    float batteryV = batteryVoltage / 1000.0;

    // === PROTECTION BATTERIE ===
    // Si la batterie est trop faible, on n'envoie pas et on retourne dormir
    if (batteryV < BATTERY_CRITICAL_V) {
        Serial.print("BATTERIE CRITIQUE : ");
        Serial.print(batteryV, 2);
        Serial.println("V - Pas d'envoi");
        return;
    }

    // Lecture solaire
    uint16_t solarADC = analogRead(SOLAR_PIN);
    uint32_t solarVoltage = esp_adc_cal_raw_to_voltage(solarADC, &adc_chars) * 2;

    // Lecture vent
    float windDirection = weatherMeterKit.getWindDirection();
    float windSpeed = weatherMeterKit.getWindSpeed();
    delay(3000);
    float windSpeedTheSecond = weatherMeterKit.getWindSpeed();
    windSpeed = (windSpeed + windSpeedTheSecond) / 2;

    // Calcul pluie depuis dernier envoi
    float totalRainfall = (rainCount - lastSentRainCount) * 0.2794;

    // null au lieu de nan si le DHT22 ne repond pas
    String tempStr = isnan(temperature) ? "null" : String(temperature, 2);
    String humStr = isnan(humidity) ? "null" : String(humidity, 2);

    // Construction JSON
    String post_body = "{\"temperature\":" + tempStr +
                   ", \"humidity\":" + humStr +
                   ", \"battery_voltage\":" + String(batteryV, 2) +
                   ", \"solar_charging_voltage\":" + String(solarVoltage / 1000.0, 2) +
                   ", \"wind_heading\":" + String(windDirection, 3) +
                   ", \"wind_speed\":" + String(windSpeed, 3) +
                   ", \"total_rainfall\":" + String(totalRainfall, 3) +
                   ", \"cumulative_rainfall\":" + String(cumulativeRainCount * 0.2794, 3) + "}";

    Serial.println(post_body);

    int httpCode = sendHttpPost(server_url, post_body);
    if (httpCode != 200) {
        Serial.print("HTTP POST request failed! Error code = ");
        Serial.println(httpCode);
    } else {
        Serial.println("Data sent successfully!");
    }

    lastSentRainCount = rainCount;
}

// === BOUCLE PRINCIPALE ===
void loop() {
    readAndSendData();

    if (wokeFromRain) {
        Serial.println("Mode pluie actif - 6 minutes max");
        unsigned long rainStart = millis();
        int extensions = 0;

        while (millis() - rainStart < RAIN_AWAKE_MS) {
            if (rainCount > lastSentRainCount && extensions < RAIN_MAX_EXTENSIONS) {
                delay(5000);
                readAndSendData();
                extensions++;
                // On NE remet PAS rainStart a zero !
                // Le timer continue de compter les 6 minutes depuis le debut.
            }
            delay(100);
        }

        Serial.println("Fin du mode pluie");
        wokeFromRain = false;
    }

    goToSleep();
}

// === DORMIR ===
void goToSleep() {
    Serial.println("Preparing to sleep");

    pinMode(MODEM_DTR_PIN, OUTPUT);
    digitalWrite(MODEM_DTR_PIN, HIGH);
    gpio_hold_en((gpio_num_t)MODEM_DTR_PIN);

    digitalWrite(BOARD_POWERON_PIN, LOW);

    // Deux sources de reveil :
    // 1. Timer (5 minutes)
    // 2. Pluviometre (GPIO 32, niveau LOW)
    unsigned long sleepTime = TIME_TO_SLEEP;
    esp_sleep_enable_timer_wakeup(sleepTime * uS_TO_S_FACTOR);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_32, 0);

    Serial.println("Going to sleep now");
    delay(1000);
    esp_deep_sleep_start();
}
