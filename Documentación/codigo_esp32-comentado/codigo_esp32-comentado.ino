/************************************************************
 * EcoSmart – Sistema de clasificación automática de residuos
 * ESP32 + ULN2003 + 28BYJ-48 + HOMING (X_min)
 * 3 tachos: Metal / Orgánico / Resto
 * Sensores: SHT31 (ΔT/ΔHR) + 3×HX711
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

/*==================== CONFIGURACIÓN RED / API ====================*/
const char* WIFI_SSID = "LABO";
const char* WIFI_PASS = "";
String API_BASE       = "http://192.168.1.100:5000";   // IP del backend

/*============== UMBRALES / CAPACIDAD ==============*/
const float CAPACITY_KG = 5.0f;   // Capacidad máxima por tacho (kg)
uint8_t ALERT_THRESHOLD = 80;     // % de llenado para alerta visual

/*===================== CONFIGURACIÓN PINOUT ======================*/
// I2C
const int PIN_SDA = 21;
const int PIN_SCL = 22;

// Sensores digitales
const int PIN_IR_OBS  = 26; // Sensor IR obstáculo (HIGH = objeto detectado)
const int PIN_IND_MET = 27; // Sensor inductivo NPN (HIGH = metal detectado)

// NeoPixel
const int PIN_NEOPIX  = 23;
const int NEOPIX_N    = 16;  // Número de LEDs
Adafruit_NeoPixel pixels(NEOPIX_N, PIN_NEOPIX, NEO_GRB + NEO_KHZ800);

// Buzzer
const int PIN_BUZZER  = 14;

// HX711 (celdas de carga)
const int PIN_HX_SCK  = 25;  // Clock común
const int PIN_HX_DT[3]= {34, 35, 36}; // Data pins (solo entrada)

// PCA9685 (control servo)
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
const int SERVO_CH       = 0;
const int SERVO_MIN_US   = 500;   // Pulso mínimo en microsegundos
const int SERVO_MAX_US   = 2400;  // Pulso máximo en microsegundos
const int SERVO_CLOSED   = 0;     // Posición cerrado (grados)
const int SERVO_OPEN     = 60;    // Posición abierto (grados)

// Motor paso a paso (ULN2003 + 28BYJ-48)
const int PIN_STP_IN1 = 18;
const int PIN_STP_IN2 = 19;
const int PIN_STP_IN3 = 5;
const int PIN_STP_IN4 = 17;

// Endstop (fin de carrera)
const int  PIN_ENDSTOP      = 16;   // INPUT_PULLUP; LOW = presionado
const int  HOME_BOUNCE_MM   = 3;    // Distancia de retroceso después de homing
const float HOME_FEED_MM_S  = 40.0; // Velocidad rápida homing
const float HOME_KISS_MM_S  = 20.0; // Velocidad lenta homing

/*======== PARÁMETROS CINEMÁTICOS EJE (configurables via API) ========*/
float stepsPerMm   = 2.5f;                 // Pasos por milímetro
float vmax_mm_s    = 120.0f;               // Velocidad máxima (mm/s)
float acc_mm_s2    = 400.0f;               // Aceleración (mm/s²)
int   pos_mm_bins[3] = { 0, 120, 240 };    // Posiciones de los tachos (mm)

/*================ OBJETOS / DRIVERS ================*/
Adafruit_SHT31 sht31 = Adafruit_SHT31();   // Sensor temperatura/humedad
HX711 HX[3];                               // Celdas de carga
AccelStepper stepper(AccelStepper::FULL4WIRE, PIN_STP_IN1, PIN_STP_IN3, PIN_STP_IN2, PIN_STP_IN4);

/*==================== MÁQUINA DE ESTADOS =================*/
enum State { 
  IDLE,           // Esperando
  ENTRY_SAMPLE,   // Muestreo inicial
  AIM,            // Moviendo a posición
  RELEASE,        // Liberando residuo
  WEIGHING,       // Pesando
  RESET           // Volviendo a posición inicial
};
State state = IDLE;

// Valores de referencia para temperatura/humedad
float baseT = NAN, baseH = NAN;

// Calibración HX711
float hxOffset[3] = { 0, 0, 0 };  // Offset
float hxScale [3] = { 1, 1, 1 };  // Escala

// Umbrales clasificación
float T_DH    = 1.5f; // Δ% Humedad relativa
float T_DT    = 0.5f; // Δ Temperatura (°C)
int   T_MIN_G = 30;   // Peso mínimo para confirmar depósito (gramos)

// Comportamiento después de pesar
constexpr bool RETURN_TO_HOME = true;  // Volver al tacho 1

/*============ PROTOTIPOS DE FUNCIONES ============*/
void performHoming();                   // Rutina de homing
void axisMoveToMM(float mmTarget);      // Mover eje a posición
void axisApplyKinematics();             // Aplicar parámetros cinemáticos

/*================ CLASIFICACIÓN DE MATERIALES =================*/
enum Material { MAT_RESTO, MAT_METAL, MAT_ORG };

// Obtener nombre del material
const char* matName(Material m){
  switch(m){
    case MAT_METAL: return "Metal";
    case MAT_ORG:   return "Orgánico";
    default:        return "Resto";
  }
}

// Obtener número de tacho según material
int binFor(Material m){
  if(m==MAT_METAL) return 1;
  if(m==MAT_ORG)   return 2;
  return 3;
}

// Algoritmo de clasificación
Material classifyOnce(){
  // 1) Detección de metal (sensor inductivo)
  if(digitalRead(PIN_IND_MET)==HIGH) return MAT_METAL;

  // 2) Detección de orgánico (cambios en T/HR)
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if(isnan(baseT)||isnan(baseH)){ baseT=t; baseH=h; }
  float dT = t - baseT;
  float dH = h - baseH;
  if(dH >= T_DH || dT >= T_DT) return MAT_ORG;

  // 3) Por defecto: resto
  return MAT_RESTO;
}

/*================= CONTROL NEOpixel / BUZZER =================*/
// Establecer color de todos los LEDs
void setPixels(uint8_t r,uint8_t g,uint8_t b){
  for(int i=0;i<NEOPIX_N;i++) pixels.setPixelColor(i,pixels.Color(r,g,b));
  pixels.show();
}

// Sonido del buzzer
void beep(int ms=80){ 
  digitalWrite(PIN_BUZZER,HIGH); 
  delay(ms); 
  digitalWrite(PIN_BUZZER,LOW); 
}

/*==================== CONTROL SERVO (PCA9685) ====================*/
// Convertir microsegundos a ticks PWM
uint16_t usToTicks(int us){
  float tick = (us/20000.0f) * 4096.0f; // 50 Hz = 20ms periodo
  if(tick<0) tick=0; if(tick>4095) tick=4095;
  return (uint16_t)tick;
}

// Mover servo a ángulo específico
void servoWriteDeg(int deg){
  deg = constrain(deg,0,180);
  int us = map(deg, 0,180, SERVO_MIN_US, SERVO_MAX_US);
  pwm.setPWM(SERVO_CH, 0, usToTicks(us));
}

// Abrir y cerrar compuerta
void gateOpen(){ servoWriteDeg(SERVO_OPEN); }
void gateClose(){ servoWriteDeg(SERVO_CLOSED); }

/*================== COMUNICACIÓN HTTP / WiFi ==================*/
// Conectar/reconectar WiFi
bool wifiEnsure(){
  if(WiFi.status()==WL_CONNECTED) return true;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<7000) delay(120);
  return WiFi.status()==WL_CONNECTED;
}

// Solicitud GET con respuesta JSON
bool httpGetJSON(const String& path, String& out){
  if(!wifiEnsure()) return false;
  HTTPClient http; http.begin(API_BASE + path);
  int code=http.GET();
  if(code==200){ out=http.getString(); http.end(); return true; }
  http.end(); return false;
}

// Solicitud POST JSON con reintentos
bool httpPostJSON_withRetry(const String& path, const String& json){
  if(!wifiEnsure()) return false;
  HTTPClient http; int tries=0; int code=-1;
  while(tries<3){
    http.begin(API_BASE + path);
    http.addHeader("Content-Type","application/json");
    code = http.POST(json);
    http.end();
    if(code==200 || code==201 || code==202) return true;
    delay( (1<<tries) * 250 ); // Backoff exponencial
    tries++;
  }
  return false;
}

// Cola para eventos pendientes de enviar
String pendingEventJSON = "";

/*============ TELEMETRÍA Y CONTROL DEL EJE ============*/
unsigned long lastAxisReportMs = 0;
float        lastAxisPosSent   = -9999.0f;
const char*  lastAxisStateSent = "IDLE";
bool         axisHomed         = false;

// Obtener posición actual en mm
inline float axisPosMm(){ return (float)stepper.currentPosition()/stepsPerMm; }

// Enviar estado del eje al servidor
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

// Reportar estado inmediatamente
void reportAxisNow(const char* st){
  lastAxisStateSent = st;
  pushAxisState(st, axisHomed, axisPosMm());
}

// Reporte periódico de estado (1Hz o cuando cambia posición)
void reportAxisPeriodic(){
  unsigned long now = millis();
  float pos = axisPosMm();
  if ((now - lastAxisReportMs) > 1000UL || fabs(pos - lastAxisPosSent) > 1.0f){
    pushAxisState(lastAxisStateSent, axisHomed, pos);
    lastAxisReportMs = now;
    lastAxisPosSent  = pos;
  }
}

/*============ POLL DE COMANDOS DESDE DASHBOARD ============*/
void pollDashboardCommands() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll < 500) return; // Poll cada 500ms
    
    String payload;
    if (httpGetJSON("/api/axis/pending_commands", payload)) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, payload)) return;
        
        // Procesar comando JOG
        if (doc.containsKey("jog_mm")) {
            float jogMm = doc["jog_mm"];
            if (jogMm != 0.0f) {
                float targetPos = axisPosMm() + jogMm;
                axisMoveToMM(targetPos);
                lastAxisStateSent = "MOVING";
            }
        }
        
        // Procesar comando HOME
        if (doc.containsKey("home") && doc["home"] == true) {
            performHoming();
        }
    }
}

/*============ ACTUALIZACIÓN DE CONFIGURACIÓN ============*/
unsigned long lastCfgPollMs = 0;
uint32_t      lastCfgCrc    = 0;

// Aplicar parámetros cinemáticos al motor
void axisApplyKinematics(){
  stepper.setMaxSpeed( vmax_mm_s * stepsPerMm );
  stepper.setAcceleration( acc_mm_s2 * stepsPerMm );
}

// Verificar y aplicar cambios de configuración
void pollConfigIfChanged(){
  if (millis() - lastCfgPollMs < 5000) return;
  lastCfgPollMs = millis();

  String payload;
  if (!httpGetJSON("/api/config", payload)) return;

  // Calcular CRC para detectar cambios
  uint32_t crc = 2166136261u;
  for (size_t i=0;i<payload.length();++i){ 
    crc ^= (uint8_t)payload[i]; 
    crc *= 16777619u; 
  }
  if (crc == lastCfgCrc) return;
  lastCfgCrc = crc;

  StaticJsonDocument<1024> j;
  if (deserializeJson(j, payload)) return;

  bool changedKin = false;

  // Actualizar parámetros cinemáticos
  if (j.containsKey("steps_per_mm")){
    float v = j["steps_per_mm"];
    if (v>0 && v!=stepsPerMm){ stepsPerMm=v; changedKin=true; }
  }
  if (j.containsKey("v_max_mm_s")){
    float v = j["v_max_mm_s"];
    if (v>0 && v!=vmax_mm_s){ vmax_mm_s=v; changedKin=true; }
  }
  if (j.containsKey("a_max_mm_s2")){
    float v = j["a_max_mm_s2"];
    if (v>0 && v!=acc_mm_s2){ acc_mm_s2=v; changedKin=true; }
  }
  if (j.containsKey("bin_positions_mm")){
    JsonArray a = j["bin_positions_mm"];
    int i=0; for (JsonVariant v : a){ if (i<3) pos_mm_bins[i++] = v.as<int>(); }
  }
  
  // Actualizar parámetros de clasificación
  if (j.containsKey("t_dh")) T_DH = j["t_dh"];
  if (j.containsKey("t_dt")) T_DT = j["t_dt"];
  if (j.containsKey("t_min_g")) T_MIN_G = j["t_min_g"];
  if (j.containsKey("alert_threshold")) ALERT_THRESHOLD = j["alert_threshold"];
  
  if (changedKin) {
    axisApplyKinematics();
  }
}

/*================= LECTURA DE SENSORES =================*/
// Leer peso en gramos
long hxReadG(int i, int samples=12){
  if(!HX[i].is_ready()) return 0;
  long sum=0; 
  for(int k=0;k<samples;k++){ sum += HX[i].read(); }
  long raw = sum/samples;
  float g  = (raw - hxOffset[i]) / hxScale[i];
  if(g<0) g=0;
  return (long)g;
}

/*================= CONTROL DEL EJE LINEAL =================*/
// Conversión entre mm y pasos
inline long  mmToSteps(float mm){ return lround(mm * stepsPerMm); }
inline float stepsToMm(long st){  return (float)st / stepsPerMm;  }

// Verificar estado del endstop
inline bool  endstopHit(){ return digitalRead(PIN_ENDSTOP)==LOW; }

// Mover a posición absoluta
void axisMoveToMM(float mmTarget){ stepper.moveTo( mmToSteps(mmTarget) ); }

// Rutina de homing (búsqueda de cero)
void performHoming(){
  reportAxisNow("HOMING");

  // Guardar configuración actual
  float origMax = stepper.maxSpeed();
  float origAcc = stepper.acceleration();

  // Fase 1: Búsqueda rápida
  stepper.setMaxSpeed(HOME_FEED_MM_S * stepsPerMm);
  stepper.setAcceleration((HOME_FEED_MM_S * stepsPerMm) * 2);
  stepper.moveTo(stepper.currentPosition() - 100000);
  while(!endstopHit()){ stepper.run(); delay(1); }
  stepper.stop(); 
  while(stepper.isRunning()) stepper.run();

  // Fase 2: Retroceso
  stepper.setCurrentPosition(0);
  stepper.moveTo(mmToSteps(HOME_BOUNCE_MM));
  while(stepper.distanceToGo()!=0){ stepper.run(); delay(1); }

  // Fase 3: Búsqueda lenta
  stepper.setMaxSpeed(HOME_KISS_MM_S * stepsPerMm);
  stepper.setAcceleration((HOME_KISS_MM_S * stepsPerMm) * 2);
  stepper.moveTo(-mmToSteps(HOME_BOUNCE_MM + 2));
  while(!endstopHit()){ stepper.run(); delay(1); }
  stepper.stop(); 
  while(stepper.isRunning()) stepper.run();

  // Restaurar configuración y posicionar
  stepper.setCurrentPosition(0);
  stepper.setMaxSpeed(origMax);
  stepper.setAcceleration(origAcc);

  // Mover a posición inicial (Tacho 1)
  axisMoveToMM(pos_mm_bins[0]);
  while(stepper.distanceToGo()!=0){ stepper.run(); delay(1); }

  axisHomed = true;
  reportAxisNow("IDLE");
}

/*======================== SETUP INICIAL ========================*/
void setup(){
  Serial.begin(115200);

  // Configuración pines
  pinMode(PIN_IR_OBS,  INPUT);
  pinMode(PIN_IND_MET, INPUT);
  pinMode(PIN_BUZZER,  OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_ENDSTOP, INPUT_PULLUP);

  // Inicializar NeoPixel
  pixels.begin(); setPixels(0,20,0);

  // Inicializar I2C y sensores
  Wire.begin(PIN_SDA, PIN_SCL);
  sht31.begin(0x44);

  // Inicializar servo
  pwm.begin(); pwm.setPWMFreq(50);
  gateClose();

  // Inicializar celdas de carga
  for(int i=0;i<3;i++){
    HX[i].begin(PIN_HX_DT[i], PIN_HX_SCK);
    HX[i].set_gain(128);
    delay(50);
  }

  // Configurar motor
  axisApplyKinematics();

  // Conectar WiFi
  WiFi.mode(WIFI_STA);
  wifiEnsure();

  // Homing inicial
  performHoming();
  setPixels(0,80,20);
  reportAxisNow("IDLE");
}

/*========================= LOOP PRINCIPAL =======================*/
unsigned long tWinStart = 0;
Material      lastMat   = MAT_RESTO;
int           targetBin = 3;
long          g0        = 0;

// Ejecutar movimiento del motor (no bloqueante)
inline void pumpMotion(){ stepper.run(); }

// Intentar enviar eventos pendientes
void tryFlushPendingEvent(){
  if(pendingEventJSON.length()==0) return;
  if(httpPostJSON_withRetry("/api/deposits", pendingEventJSON)){
    pendingEventJSON = "";
  }
}

void loop(){
  // Tareas de fondo
  pumpMotion();                    // Control motor
  reportAxisPeriodic();           // Telemetría
  pollDashboardCommands();        // Comandos dashboard
  pollConfigIfChanged();          // Configuración

  // Máquina de estados principal
  switch(state){

    case IDLE: {
      // Actualizar valores de referencia T/HR
      float t = sht31.readTemperature();
      float h = sht31.readHumidity();
      if(!isnan(t)&&!isnan(h)){
        if(isnan(baseT)||isnan(baseH)){ baseT=t; baseH=h; }
        else { 
          baseT = baseT*0.995f + t*0.005f; // Filtro EMA
          baseH = baseH*0.995f + h*0.005f; 
        }
      }
      
      setPixels(0,40,10);         // Luz de espera
      tryFlushPendingEvent();     // Enviar datos pendientes

      // Detectar objeto
      if(digitalRead(PIN_IR_OBS)==HIGH){
        tWinStart = millis();
        lastMat   = MAT_RESTO;
        state     = ENTRY_SAMPLE; // Iniciar clasificación
        lastAxisStateSent = "SENSING";
        setPixels(20,60,0);       // Luz de detección
      }
    } break;

    case ENTRY_SAMPLE: {
      // Ventana de muestreo de 500ms
      if(millis()-tWinStart < 500){
        Material m = classifyOnce();
        // Prioridad: Metal > Orgánico > Resto
        if(m==MAT_METAL) lastMat = MAT_METAL;
        else if(m==MAT_ORG && lastMat!=MAT_METAL) lastMat = MAT_ORG;
      }else{
        // Determinar tacho destino y mover
        targetBin = binFor(lastMat);
        axisMoveToMM((float)pos_mm_bins[targetBin-1]);
        lastAxisStateSent = "MOVING";
        state = AIM;
      }
    } break;

    case AIM: {
      setPixels(40,80,0);  // Luz de movimiento
      if(stepper.distanceToGo()==0){
        reportAxisNow("POSITIONED");
        state = RELEASE;  // Llegó a posición
      }
    } break;

    case RELEASE: {
      reportAxisNow("RELEASING");
      gateOpen();         // Abrir compuerta
      delay(250);        
      gateClose();        // Cerrar compuerta
      
      g0 = hxReadG(targetBin-1, 15);  // Peso inicial
      state = WEIGHING;
      lastAxisStateSent = "WEIGHING";
    } break;

    case WEIGHING: {
      static unsigned long t0 = 0;
      if(t0==0){ t0=millis(); }
      if(millis()-t0 < 300) break;    // Esperar asentamiento
      t0=0;

      // Leer peso final
      long g1 = hxReadG(targetBin-1, 15);
      long dg = g1 - g0; 
      if(dg<0) dg=0;

      // Calcular porcentaje de llenado
      float percent = (float)g1/(CAPACITY_KG*1000.0f)*100.0f;
      int ipct = (int)percent;

      // Preparar datos para enviar
      String json = String("{\"bin\":")+targetBin+
                    ",\"material\":\""+String(matName(lastMat))+"\","+
                    "\"delta_g\":"+dg+","+
                    "\"fill_percent\":"+ipct+"}";

      // Intentar enviar inmediatamente
      if(!httpPostJSON_withRetry("/api/deposits", json)){
        pendingEventJSON = json;  // Guardar para reintentar
      }

      // Indicar alerta si es necesario
      if(percent>=ALERT_THRESHOLD){ 
        setPixels(180,80,0);  // Luz naranja
        beep(120);            // Alerta audible
      }else{
        setPixels(0,80,20);   // Luz normal
      }

      state = RESET;
      lastAxisStateSent = "MOVING";
    } break;

    case RESET: {
      // Volver a posición inicial
      float target = RETURN_TO_HOME ? (float)pos_mm_bins[0] : axisPosMm();
      axisMoveToMM(target);
      if(stepper.distanceToGo()==0){
        reportAxisNow("IDLE");
        state = IDLE;  // Ciclo completado
      }
    } break;
  }
}