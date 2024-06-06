#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
#define TINY_GSM_DEBUG Serial

#include "SparkFun_Weather_Meter_Kit_Arduino_Library.h"
#include <esp_adc_cal.h>
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "includes/requests.hpp"


// Pin definitions
#define BATTERY_PIN 35  // ADC pin for battery
#define SOLAR_PIN 34    // ADC pin for solar panel
#define ONE_WIRE_BUS 15
#define WIND_DIRECTION_PIN 36
#define WIND_SPEED_PIN 14
#define RAINFALL_PIN 13

// Initialize OneWire and DallasTemperature
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Create an instance of the weather meter kit
SFEWeatherMeterKit weatherMeterKit(WIND_DIRECTION_PIN, WIND_SPEED_PIN, RAINFALL_PIN);

void setup() {
    Serial.begin(115200);
    Serial.println(F("Weather Station Example"));

    // Initialize the weather meter kit
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
    calibrationParams.minMillisPerRainfall = 100;
    calibrationParams.kphPerCountPerSec = 2.4;
    calibrationParams.windSpeedMeasurementPeriodMillis = 1000;
    weatherMeterKit.setCalibrationParams(calibrationParams);
    weatherMeterKit.begin();

    // Set ADC resolution
    analogReadResolution(12);

    // Initialize the temperature sensor
    sensors.begin();
    Serial.println(F("Sensor calibration & sensor begin done"));

    //Init modem
    Requests modem;
    if(!modem.checkSim()) {
        Serial.println("Sim check failed");
        return;
    }
    if(!modem.registerNetwork()) {
        Serial.println("network registration failed");
        return;
    }
    if(!modem.enableNetwork()) {
        Serial.println("network enabling failed");
        return;
    }

    delay(1000);
}


void loop() {
    // Read temperature
    sensors.requestTemperatures();
    float temperature = sensors.getTempCByIndex(0);

    // Characterize ADC for accurate measurement
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    // Read and convert ADC value for battery
    uint16_t batteryADC = analogRead(BATTERY_PIN);
    uint32_t batteryVoltage = esp_adc_cal_raw_to_voltage(batteryADC, &adc_chars) * 2;

    // Read and convert ADC value for solar panel
    uint16_t solarADC = analogRead(SOLAR_PIN);
    uint32_t solarVoltage = esp_adc_cal_raw_to_voltage(solarADC, &adc_chars) * 2;

    // Read weather data
    float windDirection = weatherMeterKit.getWindDirection();
    float windSpeed = weatherMeterKit.getWindSpeed();
    float totalRainfall = weatherMeterKit.getTotalRainfall();

    if(!modem.initHttps()) {
        Serial.println("HTTPS init failed");
        return;
    }

    // Prepare the POST body
    String post_body = "{\"temperature\":" + String(temperature, 2) +
                        ", \"battery_voltage\":" + String(batteryVoltage / 1000.0, 2) +
                        ", \"solar_voltage\":" + String(solarVoltage / 1000.0, 2) +
                        ", \"wind_direction\":" + String(windDirection, 2) +
                        ", \"wind_speed\":" + String(windSpeed, 2) +
                        ", \"total_rainfall\":" + String(totalRainfall, 2) + "}";

    modem.postHttpsRequests(post_body);

    delay(60000); // Delay for 1 minute before next measurement
}
