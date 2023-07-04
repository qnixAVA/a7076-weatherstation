#include <TinyGsmClient.h>
#include <HttpClient.h>

// Paramètres de l'APN (Point d'accès du réseau mobile)
const char apn[] = "votre_apn";
const char gprsUser[] = "";
const char gprsPass[] = "";

// Paramètres du serveur Home Assistant
const char* server = "harn134.duckdns.org";
const int port = 443;
const char* path = "/api/webhook/ESP32meteo"; // Remplacez par votre chemin de webhook

// Données de la station météo
float temperature = 25.5;
float humidity = 60.0;

// Client GSM et client HTTP
TinyGsm modem;
TinyGsmClientSecure gsmClient(modem);
HttpClient http(gsmClient, server, port);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Initializing modem...");
  modem.init();
  modem.gprsConnect(apn, gprsUser, gprsPass);
  Serial.println("Modem initialized");
}

void loop() {
  // Envoyer les données de la station météo
  sendWebhook();

  delay(5000); // Intervalle entre les envois de données
}

void sendWebhook() {
  // Construire le corps de la requête
  String body = "{\"temperature\": " + String(temperature) + ", \"humidity\": " + String(humidity) + "}";

  // Configurer la requête HTTP
  http.beginRequest();
  http.post(path);
  http.sendHeader("Content-Type", "application/json");
  http.sendHeader("Content-Length", String(body.length()));
  http.beginBody();
  http.print(body);
  http.endRequest();

  // Lire la réponse du serveur
  int statusCode = http.responseStatusCode();
  String response = http.responseBody();

  Serial.print("HTTP Status Code: ");
  Serial.println(statusCode);
  Serial.print("Response: ");
  Serial.println(response);
}
