#ifndef REQUESTS_HPP
#define REQUESTS_HPP

#include <Arduino.h>
#include "utilities.h"
#include <TinyGsmClient.h>


class Requests {

private:
    TinyGsm modem;
    const String server_url = "http://rn134ha.duckdns.org/api/webhook/weather_station_webhook";
    String ueInfo;
    String ipAddress;

public:
    Requests() {

         modem(SerialAT);

         // Initialize the modem
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

         // Check modem connection
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

    }

    bool checkSim() {
        // Check SIM card
        SimStatus sim = SIM_ERROR;
        while (sim != SIM_READY) {
            sim = modem.getSimStatus();
            switch (sim) {
            case SIM_READY:
                Serial.println("SIM card online");
                break;
            case SIM_LOCKED:
                Serial.println("The SIM card is locked. Please unlock the SIM card first.");
                return false;
                break;
            default:
                break;
            }
            delay(1000);
        }
        return true;
    }


   bool registerNetwork() {

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
                   return false;
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
           return true;
   }

   String getUEInfo() {
       if (modem.getSystemInformation(ueInfo)) {
           Serial.print("Inquiring UE system information:");
           Serial.println(ueInfo);
       }


       return ueInfo;
   }

  bool enableNetwork() {
      getUEInfo();

      if (!modem.enableNetwork()) {
          Serial.println("Enable network failed!");
          return false;
          // Throw exception
      }
      return true;
  }

  String getIpAddr() {
      ipAddress = modem.getLocalIP();
      Serial.print("Network IP:"); Serial.println(ipAddress);
      return ipAddress;
  }

  bool initHttps() {
      // Initialize HTTPS
      modem.https_begin();

      // Set GET URT
      if (!modem.https_set_url(server_url)) {
          Serial.println("Failed to set the URL. Please check the validity of the URL!");
          return false;
      }

      modem.https_add_header("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6");
      modem.https_add_header("Accept-Encoding", "gzip, deflate, br");
      modem.https_add_header("Content-Type", "application/json");
      modem.https_set_accept_type("application/json");
      modem.https_set_user_agent("TinyGSM/LilyGo-A76XX");
  }

  String postHttpsRequests(String post_body) {
      // Send HTTP POST request
      int httpCode = modem.https_post(post_body);
      if (httpCode != 200) {
          Serial.print("HTTP POST request failed! Error code = ");
          Serial.println(httpCode);
          return ""; // Return error chain
      }

      // Read and print the HTTP response
      String header = modem.https_header();
      Serial.print("HTTP header:");
      Serial.println(header);

      String body = modem.https_body();
      Serial.print("HTTP body:");
      Serial.println(body);
      return body;
  }


};

#endif
