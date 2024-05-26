#include <esp_adc_cal.h>
#include <Arduino.h>

// Définissez les pins ADC pour la batterie et le panneau solaire
#define BATTERY_PIN 35  // Pin ADC pour la batterie
#define SOLAR_PIN 34    // Pin ADC pour le panneau solaire

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); // Résolution ADC de 12 bits (valeur par défaut)
}

void loop() {
  // Caractérisation de l'ADC pour une mesure précise
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

  // Lire et convertir la valeur ADC pour la batterie
  uint16_t batteryADC = analogRead(BATTERY_PIN);
  uint32_t batteryVoltage = esp_adc_cal_raw_to_voltage(batteryADC, &adc_chars) * 2;

  // Lire et convertir la valeur ADC pour le panneau solaire
  uint16_t solarADC = analogRead(SOLAR_PIN);
  uint32_t solarVoltage = esp_adc_cal_raw_to_voltage(solarADC, &adc_chars) * 2;

  // Afficher les tensions sur le moniteur série
  Serial.print("Tension batterie : ");
  Serial.print(batteryVoltage / 1000.0, 2); // Convertir en volts
  Serial.println(" V");

  Serial.print("Tension solaire : ");
  Serial.print(solarVoltage / 1000.0, 2); // Convertir en volts
  Serial.println(" V");

  delay(5000); // Attendre 5 secondes avant de lire à nouveau
}
