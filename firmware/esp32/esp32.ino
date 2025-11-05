/************************************************************
 * EcoSmart – ESP32 + ULN2003 + 28BYJ-48 + HOMING (X_min)
 * 3 tachos: Metal / Orgánico / Resto
 * Sensores: SHT31 (ΔT/ΔHR) + 3×HX711 + RELÉ JW2SN-DC12V
 * Actuación: Servo por PCA9685 (gate) + NeoPixel (directo)
 * Control eje: AccelStepper (no bloqueante)
 * 
 * DASHBOARD INTEGRADO:
 *  - Control manual del eje (JOG ±mm)
 *  - Homing desde dashboard
 *  - Configuración en tiempo real
 *  - Telemetría continua
 *
 * API:
 *  - POST /api/deposits
 *  - GET/POST /api/axis/state (telemetría)
 *  - POST /api/axis/jog  {mm:±X}
 *  - POST /api/axis/home
 *  - GET  /api/axis/pending_commands (ESP32 hace poll y ejecuta jog/home)
 *  - GET  /api/config        (aplica cinemática/posiciones SIN reiniciar)
 ************************************************************/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_PWMServoDriver.h>
#include <HX711.h>
#include <AccelStepper.h>
#include <ArduinoJson.h>
#include <math.h>

/*==================== RED / API ====================*/
const char* WIFI_SSID = "LABO";
const char* WIFI_PASS = "";
String API_BASE       = "http://172.16.211.113:5000";   // ✅ IP real de tu backend

/*============== UMBRALES / CAPACIDAD ==============*/
const float CAPACITY_KG = 1.0f;   // capacidad por tacho
uint8_t ALERT_THRESHOLD = 50;     // % alerta visual

/*===================== PINOUT ======================*/
// I2C
const int PIN_SDA = 21;
const int PIN_SCL = 22;

// Sensores digitales
const int PIN_IR_OBS  = 26; // IR obstáculo (HIGH = hay objeto)
const int PIN_IND_MET = 27; // Inductivo CON RELÉ JW2SN-DC12V - LÓGICA CORRECTA

// NeoPixel (directo al ESP32) -> usar R serie 30–50 Ω en DIN
const int PIN_NEOPIX  = 23;
const int NEOPIX_N    = 8;  // cantidad de LEDs
Adafruit_NeoPixel pixels(NEOPIX_N, PIN_NEOPIX, NEO_GRB + NEO_KHZ800);

// Buzzer
const int PIN_BUZZER  = 14;

// HX711: SCK común + 3 DOUT (GPIO34/35/36 son solo entrada)
const int PIN_HX_SCK  = 25;
const int PIN_HX_DT[3]= {34, 35, 36};

// PCA9685 (servo gate en CH0, 50 Hz)
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
const int SERVO_CH       = 0;
const int SERVO_MIN_US   = 500;   // ajustar a tu servo
const int SERVO_MAX_US   = 2400;  // ajustar a tu servo
const int SERVO_CLOSED   = 0;     // grados lógicos
const int SERVO_OPEN     = 60;    // grados lógicos

// ULN2003 + 28BYJ-48 – orden FULL4WIRE: IN1,IN3,IN2,IN4
const int PIN_STP_IN1 = 18;
const int PIN_STP_IN2 = 19;
const int PIN_STP_IN3 = 5;
const int PIN_STP_IN4 = 17;

// Endstop X_min (NC recomendado, activo-bajo)
const int  PIN_ENDSTOP      = 16;   // INPUT_PULLUP; LOW = presionado
const int  HOME_BOUNCE_MM   = 3;    // alejar para liberar
const float HOME_FEED_MM_S  = 40.0; // mm/s
const float HOME_KISS_MM_S  = 20.0; // mm/s

/*======== Cinemática eje (calibrable / API) ========*/
float stepsPerMm   = 2.5f;                 // pasos/mm (CALIBRAR)
float vmax_mm_s    = 120.0f;               // mm/s
float acc_mm_s2    = 400.0f;               // mm/s^2
int   pos_mm_bins[3] = { 0, 120, 240 };    // centros Tacho1..3 (mm)

/*================ OBJETOS / DRIVERS ================*/
Adafruit_SHT31 sht31 = Adafruit_SHT31();   // T/HR
HX711 HX[3];
AccelStepper stepper(AccelStepper::FULL4WIRE, PIN_STP_IN1, PIN_STP_IN3, PIN_STP_IN2, PIN_STP_IN4);

/*==================== ESTADO / FSM =================*/
enum State { IDLE, ENTRY_SAMPLE, AIM, RELEASE, WEIGHING, RESET };
State state = IDLE;

// baseline T/HR (EMA) para orgánicos
float baseT = NAN, baseH = NAN;

// calibración HX711: gramos = (raw - offset) / scale
float hxOffset[3] = { 250, 0, 717 };
float hxScale [3] = { 0, 209.663, 839.168 };

// umbrales ΔH/ΔT
float T_DH    = 3.0f; // Δ%HR
float T_DT    = 1.0f; // Δ°C
int   T_MIN_G = 10;   // Δg mínimo para confirmar depósito

// comportamiento tras pesar: volver o no a reposo (tacho 1)
constexpr bool RETURN_TO_HOME = true;

/*============ Prototipos necesarios ============*/
void performHoming();
void axisMoveToMM(float mmTarget);
void axisApplyKinematics();
bool isMetalDetected(); // FUNCIÓN PARA RELÉ

/*================ CLASIFICACIÓN (3 TACHOS) =================*/
enum Material { MAT_RESTO, MAT_METAL, MAT_ORG };

const char* matName(Material m){
  switch(m){
    case MAT_METAL: return "Metal";
    case MAT_ORG:   return "Orgánico";
    default:        return "Resto";
  }
}
int binFor(Material m){
  if(m==MAT_METAL) return 1;
  if(m==MAT_ORG)   return 2;
  return 3;
}

// ✅ FUNCIÓN CORREGIDA PARA DETECCIÓN CON RELÉ
bool isMetalDetected() {
  // CON RELÉ JW2SN-DC12V EN CONEXIÓN CORRECTA:
  // - Sensor detecta metal → Relé SE ACTIVA → GPIO27 va a LOW
  // - No metal → Relé INACTIVO → GPIO27 queda en HIGH (3.3V por COM)
  bool metalDetected = (digitalRead(PIN_IND_MET) == LOW);
  
  // Debug para verificar funcionamiento
  static unsigned long lastDebug = 0;
  if(millis() - lastDebug > 2000) {
    lastDebug = millis();
    Serial.print("🔍 RELÉ - GPIO27: ");
    Serial.print(digitalRead(PIN_IND_MET));
    Serial.print(" -> ");
    
    if(metalDetected) {
      Serial.println("METAL (Relé ACTIVADO) ⚡");
    } else {
      Serial.println("NO METAL (Relé DESACTIVADO) 🔌");
    }
  }
  
  return metalDetected;
}

Material classifyOnce(){
  // 1) Metal por inductivo (LÓGICA CORRECTA CON RELÉ)
  if(isMetalDetected()) {
    Serial.println("🔩 METAL detectado - Relé ACTIVADO");
    return MAT_METAL;
  }

  // 2) Orgánico por ΔH/ΔT (respecto a baseline EMA)
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if(isnan(baseT)||isnan(baseH)){ 
    baseT=t; baseH=h; 
  }
  float dT = t - baseT;
  float dH = h - baseH;
  
  // Debug de lecturas T/H
  static unsigned long lastTDebug = 0;
  if(millis() - lastTDebug > 3000) {
    lastTDebug = millis();
    Serial.print("🌡️  T: "); Serial.print(t); 
    Serial.print("°C, H: "); Serial.print(h); 
    Serial.print("%, ΔT: "); Serial.print(dT);
    Serial.print(", ΔH: "); Serial.println(dH);
  }
  
  if(dH >= T_DH || dT >= T_DT) {
    Serial.println("🍎 ORGÁNICO detectado (cambio T/H)");
    return MAT_ORG;
  }

  // 3) Resto
  Serial.println("📦 RESTO detectado");
  return MAT_RESTO;
}

/*================= NeoPixel / Buzzer =================*/
void setPixels(uint8_t r,uint8_t g,uint8_t b){
  for(int i=0;i<NEOPIX_N;i++) pixels.setPixelColor(i,pixels.Color(r,g,b));
  pixels.show();
}
void beep(int ms=80){ digitalWrite(PIN_BUZZER,HIGH); delay(ms); digitalWrite(PIN_BUZZER,LOW); }

/*==================== Servo (PCA9685) ====================*/
uint16_t usToTicks(int us){
  float tick = (us/20000.0f) * 4096.0f; // 50 Hz
  if(tick<0) tick=0; if(tick>4095) tick=4095;
  return (uint16_t)tick;
}
void servoWriteDeg(int deg){
  deg = constrain(deg,0,180);
  int us = map(deg, 0,180, SERVO_MIN_US, SERVO_MAX_US);
  pwm.setPWM(SERVO_CH, 0, usToTicks(us));
}
void gateOpen(){ servoWriteDeg(SERVO_OPEN); }
void gateClose(){ servoWriteDeg(SERVO_CLOSED); }

/*================== RED / HTTP helpers ==================*/
bool wifiEnsure(){
  if(WiFi.status()==WL_CONNECTED) return true;
  Serial.println("📡 Conectando WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<7000) {
    delay(120);
    Serial.print(".");
  }
  bool connected = (WiFi.status()==WL_CONNECTED);
  if(connected) {
    Serial.println("\n✅ WiFi conectado");
  } else {
    Serial.println("\n❌ WiFi falló");
  }
  return connected;
}
bool httpGetJSON(const String& path, String& out){
  if(!wifiEnsure()) return false;
  HTTPClient http; http.begin(API_BASE + path);
  int code=http.GET();
  if(code==200){ out=http.getString(); http.end(); return true; }
  http.end(); return false;
}
bool httpPostJSON_withRetry(const String& path, const String& json){
  if(!wifiEnsure()) return false;
  HTTPClient http; int tries=0; int code=-1;
  while(tries<3){
    http.begin(API_BASE + path);
    http.addHeader("Content-Type","application/json");
    code = http.POST(json);
    http.end();
    if(code==200 || code==201 || code==202) return true;
    delay( (1<<tries) * 250 ); // 250, 500, 1000 ms
    tries++;
  }
  return false;
}
// cola RAM mínima para reintentos diferidos
String pendingEventJSON = "";

/*============ Telemetría del eje / Axis State ============*/
unsigned long lastAxisReportMs = 0;
float        lastAxisPosSent   = -9999.0f;
const char*  lastAxisStateSent = "IDLE";
bool         axisHomed         = false;

inline float axisPosMm(){ return (float)stepper.currentPosition()/stepsPerMm; }

void pushAxisState(const char* st, bool homed, float pos_mm){
  if (WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String(API_BASE) + "/api/axis/state");
  http.addHeader("Content-Type","application/json");
  String body = String("{\"state\":\"")+st+"\",\"homed\":"+(homed?"true":"false")+
                ",\"pos_mm\":"+String(pos_mm,1)+"}";
  http.POST(body);
  http.end();
}
void reportAxisNow(const char* st){
  lastAxisStateSent = st;
  pushAxisState(st, axisHomed, axisPosMm());
}
void reportAxisPeriodic(){  // 1 Hz o si varía > 1 mm
  unsigned long now = millis();
  float pos = axisPosMm();
  if ((now - lastAxisReportMs) > 1000UL || fabs(pos - lastAxisPosSent) > 1.0f){
    pushAxisState(lastAxisStateSent, axisHomed, pos);
    lastAxisReportMs = now;
    lastAxisPosSent  = pos;
  }
}

/*============ Poll de comandos Jog/Home desde Dashboard ============*/
void pollDashboardCommands() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll < 500) return; // Poll cada 500ms
    lastPoll = millis();

    // Verificar si hay comandos pendientes del dashboard
    String payload;
    if (httpGetJSON("/api/axis/pending_commands", payload)) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, payload)) return;
        
        // Procesar comando de jog
        if (doc.containsKey("jog_mm")) {
            float jogMm = doc["jog_mm"];
            if (jogMm != 0.0f) {
                float targetPos = axisPosMm() + jogMm;
                axisMoveToMM(targetPos);
                lastAxisStateSent = "MOVING";
                Serial.printf("Comando JOG recibido: %.1f mm\n", jogMm);
            }
        }
        
        // Procesar comando de home
        if (doc.containsKey("home") && doc["home"] == true) {
            performHoming();
            Serial.println("Comando HOME recibido");
        }
    }
}

/*============ Poll de /api/config sin reiniciar ============*/
unsigned long lastCfgPollMs = 0;
uint32_t      lastCfgCrc    = 0;

void axisApplyKinematics(){
  stepper.setMaxSpeed( vmax_mm_s * stepsPerMm );        // pasos/seg
  stepper.setAcceleration( acc_mm_s2 * stepsPerMm );    // pasos/seg^2
}

void pollConfigIfChanged(){
  if (millis() - lastCfgPollMs < 5000) return;
  lastCfgPollMs = millis();

  String payload;
  if (!httpGetJSON("/api/config", payload)) return;

  // CRC FNV-1a simple
  uint32_t crc = 2166136261u;
  for (size_t i=0;i<payload.length();++i){ crc ^= (uint8_t)payload[i]; crc *= 16777619u; }
  if (crc == lastCfgCrc) return;
  lastCfgCrc = crc;

  StaticJsonDocument<1024> j;
  if (deserializeJson(j, payload)) {
    Serial.println("❌ Error parseando JSON de configuración");
    return;
  }

  bool changedKin = false;

  if (j.containsKey("steps_per_mm")){
    float v = j["steps_per_mm"].as<float>();
    if (v>0 && v!=stepsPerMm){ stepsPerMm=v; changedKin=true; }
  }
  if (j.containsKey("v_max_mm_s")){
    float v = j["v_max_mm_s"].as<float>();
    if (v>0 && v!=vmax_mm_s){ vmax_mm_s=v; changedKin=true; }
  }
  if (j.containsKey("a_max_mm_s2")){
    float v = j["a_max_mm_s2"].as<float>();
    if (v>0 && v!=acc_mm_s2){ acc_mm_s2=v; changedKin=true; }
  }
  if (j.containsKey("bin_positions_mm") && j["bin_positions_mm"].is<JsonArray>()){
    JsonArray a = j["bin_positions_mm"].as<JsonArray>();
    int i=0; for (JsonVariant v : a){ if (i<3) pos_mm_bins[i++] = v.as<int>(); }
  }
  
  // Nuevos parámetros configurables desde dashboard
  if (j.containsKey("t_dh")) T_DH = j["t_dh"].as<float>();
  if (j.containsKey("t_dt")) T_DT = j["t_dt"].as<float>();
  if (j.containsKey("t_min_g")) T_MIN_G = j["t_min_g"].as<int>();
  if (j.containsKey("alert_threshold")) ALERT_THRESHOLD = j["alert_threshold"].as<uint8_t>();
  
  if (changedKin) {
    axisApplyKinematics();
    Serial.println("✅ Configuración cinemática actualizada");
  }
}

/*================= Lecturas / Sensores =================*/
long hxReadG(int i, int samples=12){
  if(!HX[i].is_ready()) return 0;
  long sum=0; for(int k=0;k<samples;k++){ sum += HX[i].read(); }
  long raw = sum/samples;
  float g  = (raw - hxOffset[i]) / hxScale[i];
  if(g<0) g=0;
  return (long)g;
}

/*================= Eje lineal (Stepper) =================*/
inline long  mmToSteps(float mm){ return lround(mm * stepsPerMm); }
inline float stepsToMm(long st){  return (float)st / stepsPerMm;  }
inline bool  endstopHit(){ return digitalRead(PIN_ENDSTOP)==LOW; } // NC: LOW = presionado
void axisMoveToMM(float mmTarget){ stepper.moveTo( mmToSteps(mmTarget) ); }

void performHoming(){
  reportAxisNow("HOMING");

  float origMax = stepper.maxSpeed();
  float origAcc = stepper.acceleration();

  // (1) ir hacia X_min rápido hasta presionar
  stepper.setMaxSpeed(HOME_FEED_MM_S * stepsPerMm);
  stepper.setAcceleration((HOME_FEED_MM_S * stepsPerMm) * 2);
  stepper.moveTo( stepper.currentPosition() - 100000 ); // muy negativo
  while(!endstopHit()){ stepper.run(); delay(1); }
  stepper.stop(); while(stepper.isRunning()) stepper.run();

  // (2) alejar para liberar
  stepper.setCurrentPosition(0);
  stepper.moveTo( mmToSteps(HOME_BOUNCE_MM) );
  while(stepper.distanceToGo()!=0){ stepper.run(); delay(1); }

  // (3) acercamiento lento
  stepper.setMaxSpeed(HOME_KISS_MM_S * stepsPerMm);
  stepper.setAcceleration((HOME_KISS_MM_S * stepsPerMm) * 2);
  stepper.moveTo( -mmToSteps(HOME_BOUNCE_MM + 2) );
  while(!endstopHit()){ stepper.run(); delay(1); }
  stepper.stop(); while(stepper.isRunning()) stepper.run();

  // (4) fijar cero y cinemática normal
  stepper.setCurrentPosition(0);
  stepper.setMaxSpeed(origMax);
  stepper.setAcceleration(origAcc);

  // posicionar sobre Tacho 1
  axisMoveToMM(pos_mm_bins[0]);
  while(stepper.distanceToGo()!=0){ stepper.run(); delay(1); }

  axisHomed = true;
  reportAxisNow("IDLE");
}

// ✅ FUNCIÓN DE DIAGNÓSTICO MEJORADA PARA RELÉ
void diagnosticarSensores() {
  Serial.println("\n=== DIAGNÓSTICO CON RELÉ ===");
  Serial.println("🎯 Comportamiento ESPERADO:");
  Serial.println("   - GPIO27 = LOW  → METAL detectado (Relé ACTIVO)");
  Serial.println("   - GPIO27 = HIGH → NO metal (Relé INACTIVO)");
  Serial.println("🔍 Probando sensores...");
  
  for(int i = 0; i < 6; i++) {
    Serial.print("Lectura "); Serial.print(i+1); Serial.println(":");
    
    int valorGPIO = digitalRead(PIN_IND_MET);
    bool metal = isMetalDetected();
    
    Serial.print("  Metal (GPIO27): ");
    Serial.print(valorGPIO);
    Serial.print(" → ");
    
    if (valorGPIO == LOW) {
      Serial.println("LOW  = METAL DETECTADO ✓");
      Serial.println("        (Relé ACTIVADO - protección funcionando)");
    } else {
      Serial.println("HIGH = NO METAL ✓"); 
      Serial.println("        (Relé DESACTIVADO)");
    }
    
    Serial.print("  IR (26): "); 
    Serial.println(digitalRead(PIN_IR_OBS));
    
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    Serial.print("  Temp: "); Serial.print(t); 
    Serial.print("°C, Hum: "); Serial.print(h); Serial.println("%");
    
    Serial.println("---");
    delay(500);
  }
  
  Serial.print("Endstop (16): "); 
  Serial.println(digitalRead(PIN_ENDSTOP));
  Serial.println("====================================\n");
}

/*======================== SETUP ========================*/
void setup(){
  Serial.begin(115200);
  Serial.println("🚀 Iniciando EcoSmart con relé...");

  pinMode(PIN_IR_OBS,  INPUT);
  pinMode(PIN_IND_MET, INPUT);
  pinMode(PIN_BUZZER,  OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_ENDSTOP, INPUT_PULLUP);

  // NeoPixel
  pixels.begin(); setPixels(0,20,0);
  Serial.println("✅ NeoPixel inicializado");

  // I2C / SHT31
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!sht31.begin(0x44)) {
    Serial.println("❌ Error: SHT31 no encontrado!");
  } else {
    Serial.println("✅ SHT31 inicializado");
  }

  // PCA9685 (servo)
  pwm.begin(); pwm.setPWMFreq(50);
  gateClose();
  Serial.println("✅ Servo inicializado");

  // HX711 (3 canales)
  for(int i=0;i<3;i++){
    HX[i].begin(PIN_HX_DT[i], PIN_HX_SCK);
    HX[i].set_gain(128);
    delay(50);
  }
  Serial.println("✅ HX711 inicializados");

  // Stepper
  axisApplyKinematics();
  Serial.println("✅ Motor paso a paso configurado");

  // WiFi
  WiFi.mode(WIFI_STA);
  wifiEnsure();

  // ✅ DIAGNÓSTICO CON RELÉ
  diagnosticarSensores();

  // Homing inicial
  Serial.println("🔄 Iniciando homing...");
  performHoming();
  
  setPixels(0,80,20);
  reportAxisNow("IDLE");
  Serial.println("🎉 Sistema listo con relé - En estado IDLE");
}

/*========================= LOOP =======================*/
unsigned long tWinStart = 0;
Material      lastMat   = MAT_RESTO;
int           targetBin = 3;
long          g0        = 0;

inline void pumpMotion(){ stepper.run(); }

void tryFlushPendingEvent(){
  if(pendingEventJSON.length()==0) return;
  if(httpPostJSON_withRetry("/api/deposits", pendingEventJSON)){
    pendingEventJSON = "";
  }
}

void loop(){
  // mantener todo vivo
  pumpMotion();
  reportAxisPeriodic();
  pollDashboardCommands();
  pollConfigIfChanged();

  switch(state){

    case IDLE: {
      // baseline T/HR (EMA) en reposo
      float t = sht31.readTemperature();
      float h = sht31.readHumidity();
      if(!isnan(t)&&!isnan(h)){
        if(isnan(baseT)||isnan(baseH)){ baseT=t; baseH=h; }
        else { baseT = baseT*0.995f + t*0.005f; baseH = baseH*0.995f + h*0.005f; }
      }
      setPixels(0,40,10);
      tryFlushPendingEvent();

      if(digitalRead(PIN_IR_OBS)==HIGH){
        Serial.println("🎯 OBJETO DETECTADO - Iniciando clasificación...");
        tWinStart = millis();
        lastMat   = MAT_RESTO;
        state     = ENTRY_SAMPLE;
        lastAxisStateSent = "SENSING";
        setPixels(20,60,0);
      }
    } break;

    case ENTRY_SAMPLE: {
      // ~500 ms de muestreo (prioridad: Metal > Orgánico > Resto)
      if(millis()-tWinStart < 500){
        Material m = classifyOnce();
        if(m==MAT_METAL) lastMat = MAT_METAL;
        else if(m==MAT_ORG && lastMat!=MAT_METAL) lastMat = MAT_ORG;
      }else{
        targetBin = binFor(lastMat);
        Serial.print("🎯 Moviendo a tacho "); Serial.println(targetBin);
        axisMoveToMM( (float)pos_mm_bins[targetBin-1] );
        lastAxisStateSent = "MOVING";
        state = AIM;
      }
    } break;

    case AIM: {
      setPixels(40,80,0);
      if(stepper.distanceToGo()==0){
        reportAxisNow("POSITIONED");
        state = RELEASE;
      }
    } break;

    case RELEASE: {
      reportAxisNow("RELEASING");
      gateOpen();  delay(250);  gateClose();
      g0 = hxReadG(targetBin-1, 15);  // base del tacho elegido
      state = WEIGHING;
      lastAxisStateSent = "WEIGHING";
    } break;

    case WEIGHING: {
      static unsigned long t0 = 0;
      if(t0==0){ t0=millis(); }
      if(millis()-t0 < 300) break;    // asentamiento corto
      t0=0;

      long g1 = hxReadG(targetBin-1, 15);
      long dg = g1 - g0; if(dg<0) dg=0;

      float percent = (float)g1/(CAPACITY_KG*1000.0f)*100.0f;
      int ipct = (int)percent;

      // ✅ JSON CORREGIDO: usar "delta_g" para coincidir con tu backend
      String json = String("{\"bin\":")+targetBin+
                    ",\"material\":\""+String(matName(lastMat))+"\","+
                    "\"delta_g\":"+dg+","+
                    "\"fill_percent\":"+ipct+"}";

      Serial.print("📊 Enviando datos: "); Serial.println(json);

      if(!httpPostJSON_withRetry("/api/deposits", json)){
        pendingEventJSON = json;
        Serial.println("⚠️  Datos guardados en cola pendiente");
      }

      if(percent>=ALERT_THRESHOLD){ 
        setPixels(180,80,0); beep(120); 
        Serial.println("🚨 ALERTA: Tacho cerca del límite!");
      }
      else setPixels(0,80,20);

      state = RESET;
      lastAxisStateSent = "MOVING";
    } break;

    case RESET: {
      float target = RETURN_TO_HOME ? (float)pos_mm_bins[0] : axisPosMm();
      axisMoveToMM(target);
      if(stepper.distanceToGo()==0){
        reportAxisNow("IDLE");
        state = IDLE;
        Serial.println("🔄 Vuelta a IDLE completada");
      }
    } break;
  }
}
