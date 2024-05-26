#include <OneWire.h>
#include <DallasTemperature.h>

// Définir le port GPIO pour le capteur DS18B20
#define ONE_WIRE_BUS 15

// Initialiser l'instance OneWire pour communiquer avec n'importe quel appareil OneWire
OneWire oneWire(ONE_WIRE_BUS);

// Passer l'instance OneWire au capteur DallasTemperature
DallasTemperature sensors(&oneWire);

void setup() {
  // Démarrer la communication série pour le débogage
  Serial.begin(115200);
  Serial.println("Dallas Temperature DS18B20 Example");

  // Démarrer le capteur
  sensors.begin();
}

void loop() {
  // Demander au capteur de lire la température
  sensors.requestTemperatures();
  
  // Obtenir la température en degrés Celsius
  float temperatureC = sensors.getTempCByIndex(0);
  
  // Imprimer la température sur le moniteur série
  Serial.print("Température : ");
  Serial.print(temperatureC);
  Serial.println(" °C");
  
  // Attendre 1 seconde avant de lire à nouveau
  delay(1000);
}
