#include <HX711.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "PAGINA_PRESENTACION.h"
#include "PAGINA1CODE.h"  

const char* ssid = "LABO";
const char* password = "";

WebServer server(80);

#define DOUT1  34
#define DOUT2  35
#define DOUT3  36
#define CLK_COMMON  25

HX711 balanza1;
HX711 balanza2;
HX711 balanza3;

float peso1 = 0;
float peso2 = 0;
float peso3 = 0;

float calibration_factor1 = -1000;
float calibration_factor2 = -1000;
float calibration_factor3 = -1000;


void setup() {
  Serial.begin(115200);
  Serial.println("Conectando a WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConectado a la red WiFi");
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  
  inicializarCeldasCarga();

  // Iniciar mDNS
  if (!MDNS.begin("pro")) {
    Serial.println("Error al iniciar mDNS");
    return;
  }
  Serial.println("mDNS iniciado: http://pro.local");

  // Configurar rutas del servidor web
  server.on("/", HTTP_GET, handlePresentacion);  // Presentación como página principal
  server.on("/dashboard", HTTP_GET, handleDashboard);  // Dashboard en ruta separada
  server.on("/presentacion", HTTP_GET, handlePresentacion);
  server.on("/pesos", HTTP_GET, handlePesos);  // ← DESCOMENTA SI USAS CELDAS
  server.on("/tara", HTTP_GET, handleTara);    // ← DESCOMENTA SI USAS CELDAS
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/move", HTTP_GET, handleMove);
  server.on("/home", HTTP_GET, handleHome);
  server.on("/config", HTTP_POST, handleConfig);

  server.begin();
  Serial.println("Servidor web iniciado");
}

void loop() {
  server.handleClient();
  leerPesos();  // ← DESCOMENTA SI USAS CELDAS
  delay(200);
}

void handleMove() {
  String direction = server.arg("dir");
  String distance = server.arg("dist");
  
  Serial.println("🎯 Moviendo eje: " + direction + " " + distance + "mm");
  // Aquí iría el código para controlar tu motor/pasos
  
  String json = "{\"status\":\"moving\",\"dir\":\"" + direction + "\",\"dist\":\"" + distance + "\"}";
  server.send(200, "application/json", json);
}

void handleHome() {
  Serial.println("🎯 Ejecutando homing del eje");
  // Código para homing del eje
  
  server.send(200, "application/json", "{\"status\":\"homing\"}");
}

void handleConfig() {
  Serial.println("💾 Guardando configuración del eje");
  // Guardar configuración en EEPROM o variables
  
  server.send(200, "application/json", "{\"status\":\"config_saved\"}");
}


void inicializarCeldasCarga() {
  Serial.println("Inicializando celdas de carga...");
  
  balanza1.begin(DOUT1, CLK_COMMON);
  balanza2.begin(DOUT2, CLK_COMMON);
  balanza3.begin(DOUT3, CLK_COMMON);
  
  balanza1.set_scale(calibration_factor1);
  balanza2.set_scale(calibration_factor2);
  balanza3.set_scale(calibration_factor3);
  
  balanza1.tare();
  balanza2.tare();
  balanza3.tare();
  
  Serial.println("Celdas de carga inicializadas");
}

void leerPesos() {
  if (balanza1.is_ready()) {
    peso1 = balanza1.get_units(3);
    delay(10);
  }
  
  if (balanza2.is_ready()) {
    peso2 = balanza2.get_units(3);
    delay(10);
  }
  
  if (balanza3.is_ready()) {
    peso3 = balanza3.get_units(3);
    delay(10);
  }
}

void handlePesos() {
  String json = "{";
  json += "\"peso1\":" + String(peso1, 2) + ",";
  json += "\"peso2\":" + String(peso2, 2) + ",";
  json += "\"peso3\":" + String(peso3, 2);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleTara() {
  balanza1.tare();
  balanza2.tare();
  balanza3.tare();
  
  server.send(200, "text/plain", "Tara realizada");
}


void handlePresentacion() {
  server.send(200, "text/html", PAGINA_PRESENTACION);
}

void handleDashboard() {
  // Aquí iría tu dashboard original
  // Por ahora redirigimos a la presentación
  server.send(200, "text/html", PAGINA1CODE);
}

void handleInfo() {
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ssid\":\"" + String(ssid) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"mDNS\":\"http://pro.local\"";
  json += "}";
  
  server.send(200, "application/json", json);
}