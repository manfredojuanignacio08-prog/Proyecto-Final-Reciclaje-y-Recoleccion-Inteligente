/************************************************************
 * EcoSmart – Sistema de clasificación automática de residuos
 * ESP32 + ULN2003 + 28BYJ-48 + HOMING (X_min)
 * 3 tachos: Metal / Orgánico / Resto
 * 
 * COMPONENTES PRINCIPALES:
 * - Sensor SHT31 (Temperatura/Humedad) para detectar orgánicos
 * - 3×HX711 (Básculas) para pesar cada tacho
 * - Servo PCA9685 para compuerta de liberación
 * - NeoPixel para indicación visual
 * - Motor paso a paso para movimiento del eje
 * - Sensores: IR (detección objeto), inductivo (metal), endstop (límite)
 * 
 * FUNCIONAMIENTO:
 * 1. Detecta objeto con sensor IR
 * 2. Clasifica material (metal/orgánico/resto)
 * 3. Mueve eje al tacho correspondiente
 * 4. Abre compuerta y libera residuo
 * 5. Pesa el tacho y reporta a la API
 * 6. Vuelve a posición inicial
 ************************************************************/

// ==================== BIBLIOTECAS NECESARIAS ====================
#include <WiFi.h>              // Conexión WiFi
#include <HTTPClient.h>        // Cliente HTTP para API
#include <Wire.h>              // Comunicación I2C
#include <Adafruit_SHT31.h>    // Sensor temperatura/humedad
#include <Adafruit_NeoPixel.h> // LEDs direccionables
#include <Adafruit_PWMServoDriver.h> // Control servo
#include <HX711.h>             // Celda de carga
#include <AccelStepper.h>      // Control motor paso a paso
#include <ArduinoJson.h>       // Manejo JSON
#include <math.h>              // Funciones matemáticas

// ==================== CONFIGURACIÓN RED / API ====================
const char* WIFI_SSID = "LABO";    // Nombre de la red WiFi
const char* WIFI_PASS = "";        // Contraseña WiFi (vacía en este caso)
String API_BASE = "http://192.168.1.100:5000"; // Dirección del servidor backend

// ==================== CONFIGURACIÓN CAPACIDAD Y ALERTAS ====================
const float CAPACITY_KG = 1.0f;   // Capacidad máxima de cada tacho (1kg)
uint8_t ALERT_THRESHOLD = 50;     // Porcentaje de llenado para alerta (50% = 500g)

// ==================== CONFIGURACIÓN DE PINES ====================

// I2C (comunicación con sensores)
const int PIN_SDA = 21;  // Datos I2C
const int PIN_SCL = 22;  // Reloj I2C

// Sensores digitales
const int PIN_IR_OBS  = 26;  // Sensor infrarrojo - HIGH cuando detecta objeto
const int PIN_IND_MET = 27;  // Sensor inductivo - HIGH cuando detecta metal

// NeoPixel (tira de LEDs para indicación visual)
const int PIN_NEOPIX  = 23;      // Pin de datos NeoPixel
const int NEOPIX_N    = 8;      // Número de LEDs en la tira
Adafruit_NeoPixel pixels(NEOPIX_N, PIN_NEOPIX, NEO_GRB + NEO_KHZ800);

// Buzzer (altavoz para alertas audibles)
const int PIN_BUZZER  = 14;

// HX711 (básculas para los 3 tachos)
const int PIN_HX_SCK  = 25;  // Pin común de reloj para las 3 básculas
const int PIN_HX_DT[3]= {34, 35, 36}; // Pines de datos para cada báscula

// PCA9685 (controlador de servos para la compuerta)
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40); // Dirección I2C
const int SERVO_CH       = 0;        // Canal del servo
const int SERVO_MIN_US   = 500;      // Pulso mínimo en microsegundos
const int SERVO_MAX_US   = 2400;     // Pulso máximo en microsegundos
const int SERVO_CLOSED   = 0;        // Posición cerrado (grados)
const int SERVO_OPEN     = 60;       // Posición abierto (grados)

// Motor paso a paso ULN2003 + 28BYJ-48
const int PIN_STP_IN1 = 18;  // Entrada 1 del driver
const int PIN_STP_IN2 = 19;  // Entrada 2 del driver  
const int PIN_STP_IN3 = 5;   // Entrada 3 del driver
const int PIN_STP_IN4 = 17;  // Entrada 4 del driver

// Endstop (sensor de fin de carrera)
const int  PIN_ENDSTOP      = 16;   // Pin del endstop - LOW cuando está presionado
const int  HOME_BOUNCE_MM   = 3;    // Distancia de retroceso después de homing (mm)
const float HOME_FEED_MM_S  = 40.0; // Velocidad rápida para homing (mm/s)
const float HOME_KISS_MM_S  = 20.0; // Velocidad lenta para homing (mm/s)

// ==================== CONFIGURACIÓN CINEMÁTICA DEL EJE ====================
float stepsPerMm   = 2.5f;                 // Pasos por milímetro (CALIBRAR)
float vmax_mm_s    = 120.0f;               // Velocidad máxima (mm/s)
float acc_mm_s2    = 400.0f;               // Aceleración (mm/s²)
int   pos_mm_bins[3] = { 0, 120, 240 };    // Posiciones de los tachos (mm)

// ==================== OBJETOS Y CONTROLADORES ====================
Adafruit_SHT31 sht31 = Adafruit_SHT31();   // Sensor de temperatura/humedad
HX711 HX[3];                               // Array de 3 básculas
AccelStepper stepper(AccelStepper::FULL4WIRE, PIN_STP_IN1, PIN_STP_IN3, PIN_STP_IN2, PIN_STP_IN4);

// ==================== MÁQUINA DE ESTADOS ====================
// Estados posibles del sistema
enum State { 
    IDLE,           // Esperando objeto
    ENTRY_SAMPLE,   // Muestreo y clasificación
    AIM,            // Moviendo al tacho destino
    RELEASE,        // Liberando residuo
    WEIGHING,       // Pesando después de liberar
    RESET           // Volviendo a posición inicial
};
State state = IDLE;  // Estado inicial

// Variables para referencia de temperatura/humedad
float baseT = NAN, baseH = NAN;  // Valores base para detección de orgánicos

// Calibración de las básculas HX711
float hxOffset[3] = { 0, 0, 0 };  // Offset de calibración
float hxScale [3] = { 1, 1, 1 };  // Escala de calibración

// Umbrales para clasificación de orgánicos
float T_DH    = 1.5f; // Umbral de cambio de humedad (%)
float T_DT    = 0.5f; // Umbral de cambio de temperatura (°C)
int   T_MIN_G = 30;   // Peso mínimo para confirmar depósito (gramos)

// Comportamiento después de pesar
constexpr bool RETURN_TO_HOME = true;  // Volver al tacho 1 después de pesar

// ==================== PROTOTIPOS DE FUNCIONES ====================
void performHoming();                   // Rutina de búsqueda de cero
void axisMoveToMM(float mmTarget);      // Mover eje a posición absoluta
void axisApplyKinematics();             // Aplicar parámetros cinemáticos

// ==================== SISTEMA DE CLASIFICACIÓN ====================

// Tipos de materiales
enum Material { MAT_RESTO, MAT_METAL, MAT_ORG };

/**
 * Obtiene el nombre del material como string
 */
const char* matName(Material m){
    switch(m){
        case MAT_METAL: return "Metal";
        case MAT_ORG:   return "Orgánico";
        default:        return "Resto";
    }
}

/**
 * Devuelve el número de tacho para cada material
 */
int binFor(Material m){
    if(m==MAT_METAL) return 1;   // Tacho 1 para metal
    if(m==MAT_ORG)   return 2;   // Tacho 2 para orgánico
    return 3;                    // Tacho 3 para resto
}

/**
 * Clasifica el material una vez
 * Prioridad: Metal > Orgánico > Resto
 */
Material classifyOnce(){
    // 1) Primero verificar si es metal (más rápido y confiable)
    if(digitalRead(PIN_IND_MET)==HIGH) return MAT_METAL;

    // 2) Luego verificar si es orgánico por cambios en T/HR
    float t = sht31.readTemperature();
    float h = sht31.readHumidity();
    
    // Inicializar valores base si es la primera lectura
    if(isnan(baseT)||isnan(baseH)){ 
        baseT=t; 
        baseH=h; 
    }
    
    // Calcular diferencias respecto a la base
    float dT = t - baseT;
    float dH = h - baseH;
    
    // Si supera umbrales, es orgánico
    if(dH >= T_DH || dT >= T_DT) return MAT_ORG;

    // 3) Por defecto, es resto
    return MAT_RESTO;
}

// ==================== CONTROL DE NEOpixel Y BUZZER ====================

/**
 * Establece el color de todos los LEDs NeoPixel
 */
void setPixels(uint8_t r, uint8_t g, uint8_t b){
    for(int i=0; i<NEOPIX_N; i++) {
        pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
    pixels.show();
}

/**
 * Emite un sonido con el buzzer
 */
void beep(int ms=80){ 
    digitalWrite(PIN_BUZZER, HIGH); 
    delay(ms); 
    digitalWrite(PIN_BUZZER, LOW); 
}

// ==================== CONTROL DEL SERVO (COMPUERTA) ====================

/**
 * Convierte microsegundos a ticks PWM para PCA9685
 */
uint16_t usToTicks(int us){
    float tick = (us/20000.0f) * 4096.0f; // 50 Hz = 20ms periodo
    if(tick<0) tick=0; 
    if(tick>4095) tick=4095;
    return (uint16_t)tick;
}

/**
 * Mueve el servo a un ángulo específico
 */
void servoWriteDeg(int deg){
    deg = constrain(deg, 0, 180);  // Limitar ángulo entre 0-180°
    int us = map(deg, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
    pwm.setPWM(SERVO_CH, 0, usToTicks(us));
}

/**
 * Abre la compuerta
 */
void gateOpen(){ 
    servoWriteDeg(SERVO_OPEN); 
}

/**
 * Cierra la compuerta  
 */
void gateClose(){ 
    servoWriteDeg(SERVO_CLOSED); 
}

// ==================== COMUNICACIÓN WiFi Y HTTP ====================

/**
 * Asegura la conexión WiFi, reconecta si es necesario
 */
bool wifiEnsure(){
    if(WiFi.status() == WL_CONNECTED) return true;
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long t0 = millis();
    
    // Intentar conectar por máximo 7 segundos
    while(WiFi.status() != WL_CONNECTED && millis()-t0 < 7000) {
        delay(120);
    }
    
    return WiFi.status() == WL_CONNECTED;
}

/**
 * Realiza una petición GET y devuelve el JSON
 */
bool httpGetJSON(const String& path, String& out){
    if(!wifiEnsure()) return false;
    
    HTTPClient http; 
    http.begin(API_BASE + path);
    int code = http.GET();
    
    if(code == 200){ 
        out = http.getString(); 
        http.end(); 
        return true; 
    }
    
    http.end(); 
    return false;
}

/**
 * Realiza una petición POST JSON con reintentos
 */
bool httpPostJSON_withRetry(const String& path, const String& json){
    if(!wifiEnsure()) return false;
    
    HTTPClient http; 
    int tries = 0; 
    int code = -1;
    
    // Reintentar hasta 3 veces con backoff exponencial
    while(tries < 3){
        http.begin(API_BASE + path);
        http.addHeader("Content-Type", "application/json");
        code = http.POST(json);
        http.end();
        
        if(code == 200 || code == 201 || code == 202) return true;
        
        delay( (1 << tries) * 250 ); // 250, 500, 1000 ms
        tries++;
    }
    
    return false;
}

// Cola para eventos pendientes de enviar
String pendingEventJSON = "";

// ==================== TELEMETRÍA DEL EJE ====================
unsigned long lastAxisReportMs = 0;  // Último reporte de telemetría
float lastAxisPosSent = -9999.0f;    // Última posición reportada
const char* lastAxisStateSent = "IDLE"; // Último estado reportado
bool axisHomed = false;              // Si el eje ha hecho homing

/**
 * Obtiene la posición actual en mm
 */
inline float axisPosMm(){ 
    return (float)stepper.currentPosition() / stepsPerMm; 
}

/**
 * Envía el estado del eje a la API
 */
void pushAxisState(const char* st, bool homed, float pos_mm){
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.begin(String(API_BASE) + "/api/axis/state");
    http.addHeader("Content-Type", "application/json");
    
    String body = String("{\"state\":\"") + st + "\",\"homed\":" + 
                 (homed ? "true" : "false") + ",\"pos_mm\":" + 
                 String(pos_mm, 1) + "}";
                 
    http.POST(body);
    http.end();
}

/**
 * Reporta el estado del eje inmediatamente
 */
void reportAxisNow(const char* st){
    lastAxisStateSent = st;
    pushAxisState(st, axisHomed, axisPosMm());
}

/**
 * Reporte periódico del estado (cada 1s o cuando cambia posición)
 */
void reportAxisPeriodic(){  
    unsigned long now = millis();
    float pos = axisPosMm();
    
    if ((now - lastAxisReportMs) > 1000UL || fabs(pos - lastAxisPosSent) > 1.0f){
        pushAxisState(lastAxisStateSent, axisHomed, pos);
        lastAxisReportMs = now;
        lastAxisPosSent = pos;
    }
}

// ==================== COMANDOS DESDE EL DASHBOARD ====================

/**
 * Consulta comandos pendientes del dashboard
 */
void pollDashboardCommands() {
    static unsigned long lastPoll = 0;
    
    // Consultar cada 500ms
    if (millis() - lastPoll < 500) return; 
    lastPoll = millis();

    String payload;
    if (httpGetJSON("/api/axis/pending_commands", payload)) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, payload)) return;
        
        // Procesar comando de movimiento (JOG)
        if (doc.containsKey("jog_mm")) {
            float jogMm = doc["jog_mm"];
            if (jogMm != 0.0f) {
                float targetPos = axisPosMm() + jogMm;
                axisMoveToMM(targetPos);
                lastAxisStateSent = "MOVING";
                Serial.printf("Comando JOG recibido: %.1f mm\n", jogMm);
            }
        }
        
        // Procesar comando de homing
        if (doc.containsKey("home") && doc["home"] == true) {
            performHoming();
            Serial.println("Comando HOME recibido");
        }
    }
}

// ==================== CONFIGURACIÓN DESDE API ====================
unsigned long lastCfgPollMs = 0;  // Última consulta de configuración
uint32_t lastCfgCrc = 0;          // CRC de la última configuración

/**
 * Aplica los parámetros cinemáticos al motor
 */
void axisApplyKinematics(){
    stepper.setMaxSpeed(vmax_mm_s * stepsPerMm);        // Pasos por segundo
    stepper.setAcceleration(acc_mm_s2 * stepsPerMm);    // Pasos por segundo²
}

/**
 * Consulta cambios en la configuración desde la API
 */
void pollConfigIfChanged(){
    // Consultar cada 5 segundos
    if (millis() - lastCfgPollMs < 5000) return;
    lastCfgPollMs = millis();

    String payload;
    if (!httpGetJSON("/api/config", payload)) return;

    // Calcular CRC para detectar cambios
    uint32_t crc = 2166136261u;
    for (size_t i=0; i<payload.length(); ++i){ 
        crc ^= (uint8_t)payload[i]; 
        crc *= 16777619u; 
    }
    
    // Si no hay cambios, salir
    if (crc == lastCfgCrc) return;
    lastCfgCrc = crc;

    StaticJsonDocument<1024> j;
    if (deserializeJson(j, payload)) {
        Serial.println("❌ Error parseando JSON de configuración");
        return;
    }

    bool changedKin = false;

    // Actualizar parámetros cinemáticos si han cambiado
    if (j.containsKey("steps_per_mm")){
        float v = j["steps_per_mm"].as<float>();
        if (v>0 && v!=stepsPerMm){ 
            stepsPerMm=v; 
            changedKin=true; 
        }
    }
    if (j.containsKey("v_max_mm_s")){
        float v = j["v_max_mm_s"].as<float>();
        if (v>0 && v!=vmax_mm_s){ 
            vmax_mm_s=v; 
            changedKin=true; 
        }
    }
    if (j.containsKey("a_max_mm_s2")){
        float v = j["a_max_mm_s2"].as<float>();
        if (v>0 && v!=acc_mm_s2){ 
            acc_mm_s2=v; 
            changedKin=true; 
        }
    }
    if (j.containsKey("bin_positions_mm") && j["bin_positions_mm"].is<JsonArray>()){
        JsonArray a = j["bin_positions_mm"].as<JsonArray>();
        int i=0; 
        for (JsonVariant v : a){ 
            if (i<3) pos_mm_bins[i++] = v.as<int>(); 
        }
    }
    
    // Actualizar parámetros de clasificación
    if (j.containsKey("t_dh")) T_DH = j["t_dh"].as<float>();
    if (j.containsKey("t_dt")) T_DT = j["t_dt"].as<float>();
    if (j.containsKey("t_min_g")) T_MIN_G = j["t_min_g"].as<int>();
    if (j.containsKey("alert_threshold")) ALERT_THRESHOLD = j["alert_threshold"].as<uint8_t>();
  
    if (changedKin) {
        axisApplyKinematics();
        Serial.println("✅ Configuración cinemática actualizada");
    }
}

// ==================== LECTURA DE SENSORES ====================

/**
 * Lee el peso en gramos de una báscula
 */
long hxReadG(int i, int samples=12){
    if(!HX[i].is_ready()) return 0;
    
    // Promediar varias lecturas para mayor precisión
    long sum=0; 
    for(int k=0; k<samples; k++){ 
        sum += HX[i].read(); 
    }
    
    long raw = sum / samples;
    float g = (raw - hxOffset[i]) / hxScale[i];
    if(g<0) g=0;
    
    return (long)g;
}

// ==================== CONTROL DEL EJE LINEAL ====================

/**
 * Convierte milímetros a pasos del motor
 */
inline long mmToSteps(float mm){ 
    return lround(mm * stepsPerMm); 
}

/**
 * Convierte pasos del motor a milímetros  
 */
inline float stepsToMm(long st){  
    return (float)st / stepsPerMm;  
}

/**
 * Verifica si el endstop está presionado
 */
inline bool endstopHit(){ 
    return digitalRead(PIN_ENDSTOP) == LOW; // LOW = presionado (NC)
}

/**
 * Mueve el eje a una posición absoluta en mm
 */
void axisMoveToMM(float mmTarget){ 
    stepper.moveTo(mmToSteps(mmTarget)); 
}

/**
 * Rutina de homing (búsqueda del cero)
 */
void performHoming(){
    reportAxisNow("HOMING");

    // Guardar configuración actual
    float origMax = stepper.maxSpeed();
    float origAcc = stepper.acceleration();

    // (1) Fase rápida: buscar endstop a alta velocidad
    stepper.setMaxSpeed(HOME_FEED_MM_S * stepsPerMm);
    stepper.setAcceleration((HOME_FEED_MM_S * stepsPerMm) * 2);
    stepper.moveTo(stepper.currentPosition() - 100000); // Movimiento largo negativo
    
    while(!endstopHit()){ 
        stepper.run(); 
        delay(1); 
    }
    stepper.stop(); 
    while(stepper.isRunning()) stepper.run();

    // (2) Fase de retroceso: alejarse del endstop
    stepper.setCurrentPosition(0);
    stepper.moveTo(mmToSteps(HOME_BOUNCE_MM));
    while(stepper.distanceToGo() != 0){ 
        stepper.run(); 
        delay(1); 
    }

    // (3) Fase lenta: acercamiento preciso al endstop
    stepper.setMaxSpeed(HOME_KISS_MM_S * stepsPerMm);
    stepper.setAcceleration((HOME_KISS_MM_S * stepsPerMm) * 2);
    stepper.moveTo(-mmToSteps(HOME_BOUNCE_MM + 2));
    
    while(!endstopHit()){ 
        stepper.run(); 
        delay(1); 
    }
    stepper.stop(); 
    while(stepper.isRunning()) stepper.run();

    // (4) Establecer cero y restaurar configuración
    stepper.setCurrentPosition(0);
    stepper.setMaxSpeed(origMax);
    stepper.setAcceleration(origAcc);

    // Posicionar sobre el tacho 1
    axisMoveToMM(pos_mm_bins[0]);
    while(stepper.distanceToGo() != 0){ 
        stepper.run(); 
        delay(1); 
    }

    axisHomed = true;
    reportAxisNow("IDLE");
}

// ==================== CONFIGURACIÓN INICIAL ====================

/**
 * Setup inicial - se ejecuta una vez al inicio
 */
void setup(){
    Serial.begin(115200);  // Inicializar comunicación serie

    // Configurar pines de entrada/salida
    pinMode(PIN_IR_OBS,  INPUT);     // Sensor IR como entrada
    pinMode(PIN_IND_MET, INPUT);     // Sensor inductivo como entrada
    pinMode(PIN_BUZZER,  OUTPUT);    // Buzzer como salida
    digitalWrite(PIN_BUZZER, LOW);   // Apagar buzzer
    pinMode(PIN_ENDSTOP, INPUT_PULLUP); // Endstop con resistencia pull-up

    // Inicializar NeoPixel
    pixels.begin(); 
    setPixels(0, 20, 0);  // Color verde suave

    // Inicializar I2C y sensor SHT31
    Wire.begin(PIN_SDA, PIN_SCL);
    sht31.begin(0x44);

    // Inicializar servo y cerrar compuerta
    pwm.begin(); 
    pwm.setPWMFreq(50);  // Frecuencia PWM estándar para servos
    gateClose();

    // Inicializar las 3 básculas HX711
    for(int i=0; i<3; i++){
        HX[i].begin(PIN_HX_DT[i], PIN_HX_SCK);
        HX[i].set_gain(128);  // Ganancia estándar
        delay(50);  // Pequeña pausa entre inicializaciones
    }

    // Configurar motor paso a paso
    axisApplyKinematics();

    // Conectar a WiFi
    WiFi.mode(WIFI_STA);
    wifiEnsure();

    // Realizar homing inicial
    performHoming();
    setPixels(0, 80, 20);  // Color verde indicando listo
    reportAxisNow("IDLE");  // Reportar estado inicial
}

// ==================== BUCLE PRINCIPAL ====================
// Variables para la máquina de estados
unsigned long tWinStart = 0;  // Tiempo de inicio de ventana de muestreo
Material lastMat = MAT_RESTO; // Último material detectado
int targetBin = 3;            // Tacho destino
long g0 = 0;                  // Peso inicial antes de liberar

/**
 * Ejecuta el movimiento del motor (no bloqueante)
 */
inline void pumpMotion(){ 
    stepper.run(); 
}

/**
 * Intenta enviar eventos pendientes a la API
 */
void tryFlushPendingEvent(){
    if(pendingEventJSON.length() == 0) return;
    
    if(httpPostJSON_withRetry("/api/deposits", pendingEventJSON)){
        pendingEventJSON = "";  // Limpiar cola si se envió correctamente
    }
}

/**
 * Loop principal - se ejecuta continuamente
 */
void loop(){
    // ========== TAREAS DE FONDO (siempre activas) ==========
    
    // 1. Ejecutar movimiento del motor si está pendiente
    pumpMotion();
    
    // 2. Reportar telemetría periódicamente
    reportAxisPeriodic();
    
    // 3. Consultar comandos desde el dashboard
    pollDashboardCommands();
    
    // 4. Verificar cambios de configuración
    pollConfigIfChanged();

    // ========== MÁQUINA DE ESTADOS PRINCIPAL ==========
    
    switch(state){
        // ========== ESTADO: REPOSO ==========
        case IDLE: {
            // Actualizar valores de referencia de temperatura/humedad
            float t = sht31.readTemperature();
            float h = sht31.readHumidity();
            
            if(!isnan(t) && !isnan(h)){
                if(isnan(baseT) || isnan(baseH)){ 
                    // Primera lectura - establecer valores base
                    baseT = t; 
                    baseH = h; 
                } else { 
                    // Actualizar con filtro EMA (suavizado)
                    baseT = baseT * 0.995f + t * 0.005f;
                    baseH = baseH * 0.995f + h * 0.005f;
                }
            }
            
            // Indicación visual de estado reposo
            setPixels(0, 40, 10);
            
            // Intentar enviar eventos pendientes
            tryFlushPendingEvent();

            // Verificar si hay objeto en la entrada
            if(digitalRead(PIN_IR_OBS) == HIGH){
                tWinStart = millis();  // Iniciar temporizador
                lastMat = MAT_RESTO;   // Resetear clasificación
                state = ENTRY_SAMPLE;  // Cambiar a estado de muestreo
                lastAxisStateSent = "SENSING";
                setPixels(20, 60, 0);  // Cambiar color indicando detección
            }
        } break;

        // ========== ESTADO: MUESTREO Y CLASIFICACIÓN ==========
        case ENTRY_SAMPLE: {
            // Ventana de muestreo de 500ms para clasificación estable
            if(millis() - tWinStart < 500){
                Material m = classifyOnce();
                
                // Prioridad: Metal > Orgánico > Resto
                if(m == MAT_METAL) 
                    lastMat = MAT_METAL;
                else if(m == MAT_ORG && lastMat != MAT_METAL) 
                    lastMat = MAT_ORG;
                    
            } else {
                // Fin de ventana de muestreo - determinar tacho destino
                targetBin = binFor(lastMat);
                
                // Mover al tacho correspondiente
                axisMoveToMM((float)pos_mm_bins[targetBin-1]);
                lastAxisStateSent = "MOVING";
                state = AIM;  // Cambiar a estado de movimiento
            }
        } break;

        // ========== ESTADO: MOVIMIENTO AL TACHO ==========
        case AIM: {
            setPixels(40, 80, 0);  // Color indicando movimiento
            
            // Esperar a que termine el movimiento
            if(stepper.distanceToGo() == 0){
                reportAxisNow("POSITIONED");
                state = RELEASE;  // Cambiar a estado de liberación
            }
        } break;

        // ========== ESTADO: LIBERACIÓN DE RESIDUO ==========
        case RELEASE: {
            reportAxisNow("RELEASING");
            
            // Abrir compuerta, esperar, cerrar
            gateOpen();  
            delay(250);  
            gateClose();
            
            // Leer peso inicial después de liberar
            g0 = hxReadG(targetBin-1, 15);
            
            state = WEIGHING;
            lastAxisStateSent = "WEIGHING";
        } break;

        // ========== ESTADO: PESADO FINAL ==========
        case WEIGHING: {
            static unsigned long t0 = 0;
            
            if(t0 == 0){ 
                t0 = millis();  // Iniciar temporizador de asentamiento
            }
            
            if(millis() - t0 < 300) break;  // Esperar 300ms para asentamiento
            
            t0 = 0;  // Resetear temporizador

            // Leer peso final
            long g1 = hxReadG(targetBin-1, 15);
            long dg = g1 - g0; 
            if(dg < 0) dg = 0;  // Evitar valores negativos

            // Calcular porcentaje de llenado
            float percent = (float)g1 / (CAPACITY_KG * 1000.0f) * 100.0f;
            int ipct = (int)percent;

            // Preparar datos para enviar a la API
            String json = String("{\"bin\":") + targetBin +
                         ",\"material\":\"" + String(matName(lastMat)) + "\"," +
                         "\"delta_g\":" + dg + "," +
                         "\"fill_percent\":" + ipct + "}";

            // Intentar enviar inmediatamente, guardar en cola si falla
            if(!httpPostJSON_withRetry("/api/deposits", json)){
                pendingEventJSON = json;
            }

            // Activar alertas si es necesario
            if(percent >= ALERT_THRESHOLD){ 
                setPixels(180, 80, 0);  // Color naranja/alerta
                beep(120);              // Sonido de alerta
            } else {
                setPixels(0, 80, 20);   // Color normal
            }

            state = RESET;
            lastAxisStateSent = "MOVING";
        } break;

        // ========== ESTADO: RESET Y VUELTA A INICIO ==========
        case RESET: {
            // Volver a posición inicial (tacho 1) o mantener posición
            float target = RETURN_TO_HOME ? (float)pos_mm_bins[0] : axisPosMm();
            axisMoveToMM(target);
            
            if(stepper.distanceToGo() == 0){
                reportAxisNow("IDLE");
                state = IDLE;  // Volver al estado de reposo
            }
        } break;
    }
}