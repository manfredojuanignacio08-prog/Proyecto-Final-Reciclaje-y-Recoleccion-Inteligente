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
const char* WIFI_SSID = "TeleCentro-0cf5-5G";
const char* WIFI_PASS = "YTZ3GTY5MGNM";
String API_BASE       = "http://192.168.0.108:5000";

/*============== UMBRALES / CAPACIDAD ==============*/
const float CAPACITY_KG = 1.0f;
uint8_t ALERT_THRESHOLD = 50;

/*===================== PINOUT ======================*/
// I2C
const int PIN_SDA = 21;
const int PIN_SCL = 22;

// Sensores digitales
const int PIN_IR_OBS  = 26;
const int PIN_IND_MET = 27; // ✅ CONEXIÓN CORREGIDA: COM→GPIO27, NO→3.3V

// NeoPixel
const int PIN_NEOPIX  = 23;
const int NEOPIX_N    = 8;
Adafruit_NeoPixel pixels(NEOPIX_N, PIN_NEOPIX, NEO_GRB + NEO_KHZ800);

// Buzzer
const int PIN_BUZZER  = 14;

// HX711
const int PIN_HX_SCK  = 25;
const int PIN_HX_DT[3]= {34, 35, 36};

// PCA9685
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
const int SERVO_CH       = 0;
const int SERVO_MIN_US   = 500;
const int SERVO_MAX_US   = 2400;
const int SERVO_CLOSED   = 0;
const int SERVO_OPEN     = 60;

// ULN2003 + 28BYJ-48
const int PIN_STP_IN1 = 18;
const int PIN_STP_IN2 = 19;
const int PIN_STP_IN3 = 5;
const int PIN_STP_IN4 = 17;

// Endstop
const int  PIN_ENDSTOP      = 16;
const int  HOME_BOUNCE_MM   = 3;
const float HOME_FEED_MM_S  = 40.0;
const float HOME_KISS_MM_S  = 20.0;

/*======== Cinemática eje ========*/
float stepsPerMm   = 2.5f;
float vmax_mm_s    = 120.0f;
float acc_mm_s2    = 400.0f;
int   pos_mm_bins[3] = { 0, 120, 240 };

/*================ OBJETOS / DRIVERS ================*/
Adafruit_SHT31 sht31 = Adafruit_SHT31();
HX711 HX[3];
AccelStepper stepper(AccelStepper::FULL4WIRE, PIN_STP_IN1, PIN_STP_IN3, PIN_STP_IN2, PIN_STP_IN4);

/*==================== ESTADO / FSM =================*/
enum State { IDLE, ENTRY_SAMPLE, AIM, RELEASE, WEIGHING, RESET };
State state = IDLE;

float baseT = NAN, baseH = NAN;
float hxOffset[3] = { 250, 0, 717 };
float hxScale [3] = { 0, 209.663, 839.168 };
float T_DH    = 3.0f;
float T_DT    = 1.0f;
int   T_MIN_G = 10;
constexpr bool RETURN_TO_HOME = true;

/*============ Prototipos ============*/
void performHoming();
void axisMoveToMM(float mmTarget);
void axisApplyKinematics();
bool isMetalDetected();
void diagnosticarSensores();

/*================ CLASIFICACIÓN =================*/
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

// ✅ FUNCIÓN CORREGIDA PARA NUEVA CONEXIÓN DEL RELÉ
bool isMetalDetected() {
  // ✅ CONEXIÓN: COM→GPIO27, NO→3.3V
  // - SIN metal: Relé INACTIVO → GPIO27 = LOW (flotante, sin conexión)
  // - CON metal: Relé ACTIVADO → GPIO27 = HIGH (3.3V a través de NO)
  bool metalDetected = (digitalRead(PIN_IND_MET) == HIGH);
  
  // Debug mejorado
  static unsigned long lastDebug = 0;
  static bool lastState = false;
  
  if(millis() - lastDebug > 2000) {
    lastDebug = millis();
    
    if(metalDetected != lastState) {
      lastState = metalDetected;
      Serial.print("🔍 SENSOR METAL - GPIO27: ");
      Serial.print(digitalRead(PIN_IND_MET));
      Serial.print(" -> ");
      
      if(metalDetected) {
        Serial.println("✅ METAL DETECTADO (Relé ACTIVADO)");
      } else {
        Serial.println("❌ NO METAL (Relé DESACTIVADO)");
      }
    }
  }
  
  return metalDetected;
}

Material classifyOnce(){
  // 1) Metal por inductivo - CON FILTRO MEJORADO
  static unsigned long lastMetalTime = 0;
  static bool lastMetalState = false;
  
  bool currentMetal = isMetalDetected();
  
  // Filtro de debounce para evitar falsos positivos
  if(currentMetal && !lastMetalState) {
    lastMetalTime = millis();
  }
  
  // Requerir detección consistente por 200ms
  if(currentMetal && (millis() - lastMetalTime > 200)) {
    Serial.println("🔩 METAL confirmado - Clasificación final");
    lastMetalState = true;
    return MAT_METAL;
  }
  
  lastMetalState = currentMetal;

  // 2) Orgánico por ΔH/ΔT (solo si no hay metal)
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  
  if(isnan(baseT) || isnan(baseH)){ 
    baseT = t; 
    baseH = h; 
  }
  
  float dT = fabs(t - baseT);
  float dH = fabs(h - baseH);
  
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

void beep(int ms=80){ 
  digitalWrite(PIN_BUZZER,HIGH); 
  delay(ms); 
  digitalWrite(PIN_BUZZER,LOW); 
}

/*==================== Servo ====================*/
uint16_t usToTicks(int us){
  float tick = (us/20000.0f) * 4096.0f;
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
  if(WiFi.status() == WL_CONNECTED) return true;
  
  if(WiFi.status() == WL_CONNECTING) {
    Serial.println("⏳ WiFi conectando, esperando...");
    unsigned long t0 = millis();
    while(WiFi.status() == WL_CONNECTING && millis() - t0 < 10000) {
      delay(500);
      Serial.print(".");
    }
    if(WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi conectado después de espera");
      return true;
    }
  }
  
  Serial.println("📡 Iniciando conexión WiFi...");
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  unsigned long t0 = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
    Serial.print(".");
  }
  
  bool connected = (WiFi.status() == WL_CONNECTED);
  if(connected) {
    Serial.println("\n✅ WiFi conectado");
    Serial.print("📶 IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi falló después de 15s");
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
    delay( (1<<tries) * 250 );
    tries++;
  }
  return false;
}

String pendingEventJSON = "";

/*============ Telemetría del eje ============*/
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

void reportAxisPeriodic(){
  unsigned long now = millis();
  float pos = axisPosMm();
  if ((now - lastAxisReportMs) > 1000UL || fabs(pos - lastAxisPosSent) > 1.0f){
    pushAxisState(lastAxisStateSent, axisHomed, pos);
    lastAxisReportMs = now;
    lastAxisPosSent  = pos;
  }
}

/*============ Poll de comandos Dashboard ============*/
void pollDashboardCommands() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll < 500) return;
    lastPoll = millis();

    String payload;
    if (httpGetJSON("/api/axis/pending_commands", payload)) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, payload)) return;
        
        if (doc.containsKey("jog_mm")) {
            float jogMm = doc["jog_mm"];
            if (jogMm != 0.0f) {
                float targetPos = axisPosMm() + jogMm;
                axisMoveToMM(targetPos);
                lastAxisStateSent = "MOVING";
                Serial.printf("Comando JOG recibido: %.1f mm\n", jogMm);
            }
        }
        
        if (doc.containsKey("home") && doc["home"] == true) {
            performHoming();
            Serial.println("Comando HOME recibido");
        }
    }
}

/*============ Poll de configuración ============*/
unsigned long lastCfgPollMs = 0;
uint32_t      lastCfgCrc    = 0;

void axisApplyKinematics(){
  stepper.setMaxSpeed( vmax_mm_s * stepsPerMm );
  stepper.setAcceleration( acc_mm_s2 * stepsPerMm );
}

void pollConfigIfChanged(){
  if (millis() - lastCfgPollMs < 5000) return;
  lastCfgPollMs = millis();

  String payload;
  if (!httpGetJSON("/api/config", payload)) return;

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

/*================= Eje lineal =================*/
inline long  mmToSteps(float mm){ return lround(mm * stepsPerMm); }
inline float stepsToMm(long st){  return (float)st / stepsPerMm;  }
inline bool  endstopHit(){ return digitalRead(PIN_ENDSTOP)==LOW; }
void axisMoveToMM(float mmTarget){ stepper.moveTo( mmToSteps(mmTarget) ); }

void performHoming(){
  reportAxisNow("HOMING");

  float origMax = stepper.maxSpeed();
  float origAcc = stepper.acceleration();

  // (1) ir hacia X_min rápido hasta presionar
  stepper.setMaxSpeed(HOME_FEED_MM_S * stepsPerMm);
  stepper.setAcceleration((HOME_FEED_MM_S * stepsPerMm) * 2);
  stepper.moveTo( stepper.currentPosition() - 100000 );
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

  axisMoveToMM(pos_mm_bins[0]);
  while(stepper.distanceToGo()!=0){ stepper.run(); delay(1); }

  axisHomed = true;
  reportAxisNow("IDLE");
}

// ✅ FUNCIÓN DE DIAGNÓSTICO ACTUALIZADA
void diagnosticarSensores() {
  Serial.println("\n=== DIAGNÓSTICO CON NUEVA CONEXIÓN ===");
  Serial.println("🎯 CONEXIÓN ACTUAL:");
  Serial.println("   COM → GPIO27");
  Serial.println("   NO  → 3.3V");
  Serial.println("   DC+ → 12V");
  Serial.println("   DC- → GND");
  Serial.println("🎯 Comportamiento ESPERADO:");
  Serial.println("   - GPIO27 = HIGH → METAL detectado (Relé ACTIVADO)");
  Serial.println("   - GPIO27 = LOW  → NO metal (Relé DESACTIVADO)");
  Serial.println("🔍 Probando sensores durante 10 segundos...");
  
  unsigned long startTime = millis();
  int metalCount = 0;
  int totalReadings = 0;
  
  while(millis() - startTime < 10000) {
    totalReadings++;
    
    int valorGPIO = digitalRead(PIN_IND_MET);
    bool metal = (valorGPIO == HIGH);
    
    if(metal) metalCount++;
    
    Serial.print("Tiempo: "); 
    Serial.print((millis() - startTime)/1000); 
    Serial.print("s - ");
    Serial.print("GPIO27: "); 
    Serial.print(valorGPIO);
    Serial.print(" → ");
    
    if (metal) {
      Serial.println("METAL DETECTADO ⚡ (Relé ACTIVADO)");
    } else {
      Serial.println("NO METAL 🔌 (Relé DESACTIVADO)");
    }
    
    Serial.print("  IR (26): "); 
    Serial.println(digitalRead(PIN_IR_OBS));
    
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    if(!isnan(t) && !isnan(h)) {
      Serial.print("  Temp: "); Serial.print(t); 
      Serial.print("°C, Hum: "); Serial.print(h); Serial.println("%");
    } else {
      Serial.println("  Error leyendo SHT31");
    }
    
    Serial.println("---");
    delay(1000);
  }
  
  Serial.println("\n📊 RESUMEN DIAGNÓSTICO:");
  Serial.print("Total lecturas: "); Serial.println(totalReadings);
  Serial.print("Detecciones METAL: "); Serial.println(metalCount);
  Serial.print("Porcentaje METAL: "); 
  Serial.print((metalCount * 100.0) / totalReadings); 
  Serial.println("%");
  
  Serial.print("Endstop (16): "); 
  Serial.println(digitalRead(PIN_ENDSTOP));
  
  // Análisis de resultados
  if(metalCount == 0) {
    Serial.println("💡 SUGERENCIA: El sensor siempre muestra NO METAL");
    Serial.println("   Verificar: Conexión 3.3V a NO, +12V a DC+");
  } else if(metalCount == totalReadings) {
    Serial.println("💡 SUGERENCIA: El sensor siempre muestra METAL");
    Serial.println("   Verificar: Sensor inductivo funcionando correctamente");
  }
  
  Serial.println("====================================\n");
}

/*======================== SETUP ========================*/
void setup(){
  Serial.begin(115200);
  delay(2000);
  Serial.println("🚀 Iniciando EcoSmart con NUEVA conexión de relé...");

  // ✅ CONFIGURACIÓN ACTUALIZADA DE PINES
  pinMode(PIN_IR_OBS,  INPUT);
  pinMode(PIN_IND_MET, INPUT);  // ✅ SIN PULLUP - depende de conexión física
  pinMode(PIN_BUZZER,  OUTPUT); 
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_ENDSTOP, INPUT_PULLUP);

  pixels.begin(); 
  setPixels(0,20,0);
  Serial.println("✅ NeoPixel inicializado");

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!sht31.begin(0x44)) {
    Serial.println("❌ Error: SHT31 no encontrado!");
  } else {
    Serial.println("✅ SHT31 inicializado");
  }

  pwm.begin(); 
  pwm.setPWMFreq(50);
  gateClose();
  Serial.println("✅ Servo inicializado");

  for(int i=0;i<3;i++){
    HX[i].begin(PIN_HX_DT[i], PIN_HX_SCK);
    HX[i].set_gain(128);
    delay(50);
  }
  Serial.println("✅ HX711 inicializados");

  axisApplyKinematics();
  Serial.println("✅ Motor paso a paso configurado");

  WiFi.mode(WIFI_STA);
  wifiEnsure();

  // ✅ DIAGNÓSTICO CON NUEVA CONEXIÓN
  diagnosticarSensores();

  Serial.println("🔄 Iniciando homing...");
  performHoming();
  
  setPixels(0,80,20);
  reportAxisNow("IDLE");
  Serial.println("🎉 Sistema listo con NUEVA conexión de relé");
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
  pumpMotion();
  reportAxisPeriodic();
  pollDashboardCommands();
  pollConfigIfChanged();

  switch(state){

    case IDLE: {
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
      g0 = hxReadG(targetBin-1, 15);
      state = WEIGHING;
      lastAxisStateSent = "WEIGHING";
    } break;

    case WEIGHING: {
      static unsigned long t0 = 0;
      if(t0==0){ t0=millis(); }
      if(millis()-t0 < 300) break;
      t0=0;

      long g1 = hxReadG(targetBin-1, 15);
      long dg = g1 - g0; if(dg<0) dg=0;

      float percent = (float)g1/(CAPACITY_KG*1000.0f)*100.0f;
      int ipct = (int)percent;

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