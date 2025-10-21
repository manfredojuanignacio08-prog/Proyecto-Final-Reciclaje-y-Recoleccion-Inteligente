/************************************************************
 * EcoSmart – ESP32 + ULN2003 + Stepper unipolar (6 hilos) con HOMING (X_min)
 * AccelStepper (no bloqueante) + SHT31 + 3×HX711 + PCA9685 (gate)
 * FSM: IDLE → ENTRY_SAMPLE → AIM → RELEASE → WEIGHING → RESET
 * Clasificación (3 tachos): Inductivo(Metal) > Orgánico(ΔH/ΔT SHT31) > Resto
 *
 * Telemetría del eje: POST /api/axis/state
 * Registro de depósitos: POST /api/deposits (con reintentos/backoff)
 * Carga opcional de cinemática/posiciones: GET /api/axis (si existe)
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

/* ===================== RED / API ===================== */
const char* WIFI_SSID = "TU_SSID";
const char* WIFI_PASS = "TU_PASSWORD";
String API_BASE       = "http://127.0.0.1:5000";   // Cambiá a la IP de tu backend

/* ================== UMBRALES / CAPACIDAD ================== */
const float CAPACITY_KG = 5.0f;   // capacidad por tacho
uint8_t ALERT_THRESHOLD = 80;     // % alerta visual

/* ======================= PINOUT ESP32 ===================== */
// I2C
const int PIN_SDA = 21;
const int PIN_SCL = 22;

// Sensores digitales
const int PIN_IR_OBS  = 26; // IR obstáculo (HIGH = hay objeto)
const int PIN_IND_MET = 27; // Inductivo NPN (adaptado a 3.3 V), HIGH = metal

// NeoPixel (opcional) vía 74HCT14 + R serie → 5 V
const int PIN_NEOPIX  = 23;
const int NEOPIX_N    = 16;

// Buzzer 5 V (NPN)
const int PIN_BUZZER  = 2;

// HX711: SCK común + 3 DOUT (1 por tacho)
const int PIN_HX_SCK  = 25;
const int PIN_HX_DT[3]= {34, 35, 36}; // sólo entrada

// PCA9685 (servo gate en CH0)
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
const int SERVO_CH       = 0;
const int SERVO_MIN_US   = 500;   // ajustar a tu servo
const int SERVO_MAX_US   = 2400;  // ajustar a tu servo
const int SERVO_CLOSED   = 0;     // grados lógicos
const int SERVO_OPEN     = 60;    // grados lógicos

// ULN2003 + 28BYJ-48 – orden FULL4WIRE requerido: IN1,IN3,IN2,IN4
const int PIN_STP_IN1 = 18;
const int PIN_STP_IN2 = 19;
const int PIN_STP_IN3 = 5;
const int PIN_STP_IN4 = 17;

// Endstop X_min (NC recomendado, activo-bajo)
const int  PIN_ENDSTOP      = 16;   // INPUT_PULLUP; LOW = presionado
const int  HOME_BOUNCE_MM   = 3;    // alejar para liberar
const float HOME_FEED_MM_S  = 40.0; // mm/s
const float HOME_KISS_MM_S  = 20.0; // mm/s

/* ======= Cinemática eje (calibrable / API) ======= */
float stepsPerMm   = 2.5f;                 // pasos/mm (CALIBRAR)
float vmax_mm_s    = 120.0f;               // mm/s
float acc_mm_s2    = 400.0f;               // mm/s^2
int   pos_mm_bins[3] = { 0, 120, 240 };    // centros Tacho1..3 (mm)

/* ============== OBJETOS / DRIVERS ============== */
Adafruit_SHT31 sht31 = Adafruit_SHT31();   // T/HR
Adafruit_NeoPixel pixels(NEOPIX_N, PIN_NEOPIX, NEO_GRB + NEO_KHZ800);
HX711 HX[3];
AccelStepper stepper(AccelStepper::FULL4WIRE, PIN_STP_IN1, PIN_STP_IN3, PIN_STP_IN2, PIN_STP_IN4);

/* =================== ESTADO / FSM =================== */
enum State { IDLE, ENTRY_SAMPLE, AIM, RELEASE, WEIGHING, RESET };
State state = IDLE;

// Baseline de T/HR (EMA) para detectar orgánicos
float baseT = NAN, baseH = NAN;

// Calibración HX711: gramos = (raw - offset) / scale
float hxOffset[3] = { 0, 0, 0 };
float hxScale [3] = { 1, 1, 1 };

// Umbrales de orgánico (sin LDR)
float T_DH    = 3.0f; // Δ%HR
float T_DT    = 1.0f; // Δ°C
int   T_MIN_G = 30;   // Δg mínimo para confirmar depósito

/* ===================== CLASIFICACIÓN (3 TACHOS) ====================== */
// 1) Tipos de material
enum Material { MAT_RESTO, MAT_METAL, MAT_ORG };

// 2) Nombre legible
const char* matName(Material m){
  switch(m){
    case MAT_METAL: return "Metal";
    case MAT_ORG:   return "Orgánico";
    default:        return "Resto";
  }
}

// 3) Mapeo a tacho
int binFor(Material m){
  if(m==MAT_METAL) return 1;
  if(m==MAT_ORG)   return 2;
  return 3; // Resto
}

// 4) Regla de decisión (sin LDR)
Material classifyOnce(){
  // Metal por inductivo
  if(digitalRead(PIN_IND_MET)==HIGH) return MAT_METAL;

  // Orgánico por ΔH/ΔT respecto a baseline
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if(isnan(baseT)||isnan(baseH)){ baseT=t; baseH=h; }
  float dT = t - baseT;
  float dH = h - baseH;
  if(dH >= T_DH || dT >= T_DT) return MAT_ORG;

  // Caso contrario: Resto
  return MAT_RESTO;
}

/* ========== NeoPixel / Buzzer utilitarios (opcional) ========== */
void setPixels(uint8_t r,uint8_t g,uint8_t b){
  for(int i=0;i<NEOPIX_N;i++) pixels.setPixelColor(i,pixels.Color(r,g,b));
  pixels.show();
}
void beep(int ms=80){ digitalWrite(PIN_BUZZER,HIGH); delay(ms); digitalWrite(PIN_BUZZER,LOW); }

/* ======================== Servo (PCA9685) ======================= */
uint16_t usToTicks(int us){
  float tick = (us/20000.0f) * 4096.0f; // 50Hz
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

/* ====================== RED / HTTP helpers ===================== */
bool wifiEnsure(){
  if(WiFi.status()==WL_CONNECTED) return true;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<7000) delay(120);
  return WiFi.status()==WL_CONNECTED;
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
    delay( (1<<tries) * 250 ); // 250ms, 500ms, 1000ms
    tries++;
  }
  return false;
}
// Cola RAM mínima para reintentos diferidos
String pendingEventJSON = "";

/* ========== Telemetría del eje → /api/axis/state (opcional) ========== */
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
unsigned long lastAxisReportMs = 0;
float        lastAxisPosSent   = -9999.0f;
const char*  lastAxisStateSent = "IDLE";
bool         axisHomed         = false;
inline float axisPosMm(){ return (float)stepper.currentPosition()/stepsPerMm; }
void reportAxisNow(const char* st){
  lastAxisStateSent = st;
  pushAxisState(st, axisHomed, axisPosMm());
}
void reportAxisPeriodic(){ // 1 Hz ó si cambia más de 1 mm
  unsigned long now = millis();
  float pos = axisPosMm();
  if ((now - lastAxisReportMs) > 1000UL || fabsf(pos - lastAxisPosSent) > 1.0f){
    pushAxisState(lastAxisStateSent, axisHomed, pos);
    lastAxisReportMs = now;
    lastAxisPosSent  = pos;
  }
}

/* ==================== Lecturas / Sensores ==================== */
long hxReadG(int i, int samples=12){
  if(!HX[i].is_ready()) return 0;
  long sum=0; for(int k=0;k<samples;k++){ sum += HX[i].read(); }
  long raw = sum/samples;
  float g  = (raw - hxOffset[i]) / hxScale[i];
  if(g<0) g=0;
  return (long)g;
}

/* =================== Eje lineal (AccelStepper) =================== */
inline void axisApplyKinematics(){
  stepper.setMaxSpeed( vmax_mm_s * stepsPerMm );        // pasos/seg
  stepper.setAcceleration( acc_mm_s2 * stepsPerMm );    // pasos/seg^2
}
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

  // (4) fijar cero y volver a cinemática normal
  stepper.setCurrentPosition(0);
  stepper.setMaxSpeed(origMax);
  stepper.setAcceleration(origAcc);

  // Posicionar sobre Tacho1
  axisMoveToMM(pos_mm_bins[0]);
  while(stepper.distanceToGo()!=0){ stepper.run(); delay(1); }

  axisHomed = true;
  reportAxisNow("IDLE");
}

/* ========================= SETUP ========================== */
void setup(){
  Serial.begin(115200);

  pinMode(PIN_IR_OBS,  INPUT);
  pinMode(PIN_IND_MET, INPUT);
  pinMode(PIN_BUZZER,  OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_ENDSTOP, INPUT_PULLUP);

  // NeoPixel
  pixels.begin(); setPixels(0,20,0);

  // I2C / SHT31
  Wire.begin(PIN_SDA, PIN_SCL);
  sht31.begin(0x44);

  // PCA9685 (servo)
  pwm.begin(); pwm.setPWMFreq(50);
  gateClose();

  // HX711 (3 canales)
  for(int i=0;i<3;i++){
    HX[i].begin(PIN_HX_DT[i], PIN_HX_SCK);
    HX[i].set_gain(128);
    delay(50);
  }

  // Stepper
  axisApplyKinematics();

  // WiFi
  WiFi.mode(WIFI_STA);
  wifiEnsure();

  // (Opcional) Cargar cinemática y posiciones desde /api/axis
  String payload;
  if(httpGetJSON("/api/axis", payload)){
    StaticJsonDocument<1024> doc;
    if(!deserializeJson(doc, payload)){
      if(doc.containsKey("steps_per_mm")) stepsPerMm = doc["steps_per_mm"].as<float>();
      if(doc.containsKey("vmax"))         vmax_mm_s  = doc["vmax"].as<float>();
      if(doc.containsKey("acc"))          acc_mm_s2  = doc["acc"].as<float>();
      if(doc.containsKey("positions_mm") && doc["positions_mm"].is<JsonArray>()){
        JsonArray arr = doc["positions_mm"].as<JsonArray>();
        int i=0; for (JsonVariant v : arr) { if(i<3) pos_mm_bins[i++] = v.as<int>(); }
      }
      axisApplyKinematics();
    }
  }

  // Homing
  performHoming();
  setPixels(0,80,20);
  reportAxisNow("IDLE");
}

/* ========================== LOOP ========================== */
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

      // JSON del evento
      String json = String("{\"bin\":")+targetBin+
                    ",\"material\":\""+String(matName(lastMat))+"\","+
                    "\"weight_grams\":"+dg+","+
                    "\"fill_percent\":"+ipct+"}";

      if(!httpPostJSON_withRetry("/api/deposits", json)){
        pendingEventJSON = json; // reintento en IDLE
      }

      if(percent>=ALERT_THRESHOLD){ setPixels(180,80,0); beep(120); }
      else setPixels(0,80,20);

      state = RESET;
      lastAxisStateSent = "MOVING";
    } break;

    case RESET: {
      axisMoveToMM( (float)pos_mm_bins[0] ); // reposo (Tacho 1)
      if(stepper.distanceToGo()==0){
        reportAxisNow("IDLE");
        state = IDLE;
      }
    } break;
  }
}
