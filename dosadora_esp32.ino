/*
  Dosadora Granular - ESP32
  Grupo 3 - API REST para o site de controle/monitoramento
  Bibliotecas necessárias (Gerenciador de Bibliotecas do Arduino IDE):
    - HX711 (bogde/HX711)
    - ESP32Servo
    - ArduinoJson

  Endpoints expostos:
    GET  /api/status  -> { peso, bomba, dosando, meta }
    POST /api/bomba    body: { "estado": true|false }
    POST /api/servo    body: { "posicao": 0-180 }
    POST /api/dosar     body: { "meta": 500 }      -> inicia dosagem até atingir "meta" gramas
    POST /api/parar     -> aborta a dosagem e desliga tudo
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HX711.h>
#include <ESP32Servo.h>

// ---------- Configurações de rede ----------
const char* ssid  = "Visitantes_2_4";
const char* senha = "Smarth4.0";

// ---------- Pinos ----------
#define HX711_DT   4
#define HX711_SCK  5
#define PINO_BOMBA 25   // rele que aciona a bomba/valvula/rosca sem-fim
#define PINO_SERVO 27

// ---------- Calibração da célula de carga ----------
// Ajuste esse fator com um peso conhecido (ver etapa de calibração do roteiro)
float FATOR_CALIBRACAO = 420.0;

HX711 balanca;
Servo servoDosador;
WebServer server(80);

bool  bombaLigada = false;
bool  dosando     = false;
float metaGramas  = 0;
float pesoAtual   = 0;

// ---------- CORS (necessário pois o site roda fora do ESP32) ----------
void adicionarCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void tratarOptions() {
  adicionarCORS();
  server.send(204);
}

// ---------- Handlers ----------
void tratarStatus() {
  adicionarCORS();
  StaticJsonDocument<200> doc;
  doc["peso"]    = pesoAtual;
  doc["bomba"]   = bombaLigada;
  doc["dosando"] = dosando;
  doc["meta"]    = metaGramas;
  String resposta;
  serializeJson(doc, resposta);
  server.send(200, "application/json", resposta);
}

void tratarBomba() {
  adicionarCORS();
  StaticJsonDocument<100> doc;
  deserializeJson(doc, server.arg("plain"));
  bombaLigada = doc["estado"];
  digitalWrite(PINO_BOMBA, bombaLigada ? HIGH : LOW);
  server.send(200, "application/json", "{\"ok\":true}");
}

void tratarServo() {
  adicionarCORS();
  StaticJsonDocument<100> doc;
  deserializeJson(doc, server.arg("plain"));
  int posicao = doc["posicao"];
  servoDosador.write(posicao);
  server.send(200, "application/json", "{\"ok\":true}");
}

void tratarDosar() {
  adicionarCORS();
  StaticJsonDocument<100> doc;
  deserializeJson(doc, server.arg("plain"));
  metaGramas = doc["meta"];
  balanca.tare();       // zera a balança antes de iniciar a dosagem
  dosando = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void tratarParar() {
  adicionarCORS();
  dosando = false;
  bombaLigada = false;
  digitalWrite(PINO_BOMBA, LOW);
  servoDosador.write(0);
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);

  pinMode(PINO_BOMBA, OUTPUT);
  digitalWrite(PINO_BOMBA, LOW);

  servoDosador.attach(PINO_SERVO);
  servoDosador.write(0);

  balanca.begin(HX711_DT, HX711_SCK);
  balanca.set_scale(FATOR_CALIBRACAO);
  balanca.tare();

  WiFi.begin(ssid, senha);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP do ESP32: ");
  Serial.println(WiFi.localIP());   // <-- use esse IP no site (config.js)

  server.on("/api/status", HTTP_GET,     tratarStatus);
  server.on("/api/status", HTTP_OPTIONS, tratarOptions);
  server.on("/api/bomba",  HTTP_POST,    tratarBomba);
  server.on("/api/bomba",  HTTP_OPTIONS, tratarOptions);
  server.on("/api/servo",  HTTP_POST,    tratarServo);
  server.on("/api/servo",  HTTP_OPTIONS, tratarOptions);
  server.on("/api/dosar",  HTTP_POST,    tratarDosar);
  server.on("/api/dosar",  HTTP_OPTIONS, tratarOptions);
  server.on("/api/parar",  HTTP_POST,    tratarParar);
  server.on("/api/parar",  HTTP_OPTIONS, tratarOptions);

  server.begin();
}

void loop() {
  server.handleClient();

  if (balanca.is_ready()) {
    pesoAtual = balanca.get_units(5);
  }

  if (dosando) {
    bombaLigada = true;
    digitalWrite(PINO_BOMBA, HIGH);
    servoDosador.write(90);   // abre o dosador

    if (pesoAtual >= metaGramas) {
      dosando = false;
      bombaLigada = false;
      digitalWrite(PINO_BOMBA, LOW);
      servoDosador.write(0);  // fecha o dosador
    }
  }
}
