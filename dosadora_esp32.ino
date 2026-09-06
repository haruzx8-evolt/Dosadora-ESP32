/*
  Dosadora Granular - ESP32 (versão combinada com calibração e LED RGB)
  Grupo 3 - HexTech

  Mantém: botão com clique=tara e segurar 2s=calibração (1kg), LED RGB
  de status (catodo comum), leitura com filtro de zero morto e anti-spam
  no Serial.

  Adiciona: WiFi + servidor web com API REST pra conectar com o site
  (index.html), controle de bomba/rosca e servo, e dosagem automática
  até atingir uma meta em gramas.

  Bibliotecas necessárias (Gerenciador de Bibliotecas do Arduino IDE):
    - HX711 (bogde/HX711)
    - ESP32Servo
    - ArduinoJson

  Endpoints expostos:
    GET  /api/status  -> { peso, bomba, dosando, meta }
    POST /api/bomba    body: { "estado": true|false }
    POST /api/servo    body: { "posicao": 0-180 }
    POST /api/dosar     body: { "meta": 500 }      -> doses até atingir "meta" gramas
    POST /api/parar     -> aborta a dosagem e desliga tudo
*/

#include "HX711.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ===== PINOS =====
#define DT 16
#define SCK 17
#define BOTAO 4

// LED RGB (CATODO COMUM)
#define LED_R 10
#define LED_G 11
#define LED_B 12

#define PINO_BOMBA 25
#define PINO_SERVO 27

// ===== REDE =====
const char* ssid  = "SEU_WIFI";
const char* senha = "SUA_SENHA";

HX711 balanca;
Servo servoDosador;
WebServer server(80);

// ===== CALIBRAÇÃO =====
float fator = 2152.0;
bool calibrado = false;

// ===== BOTÃO =====
bool estadoAnt = HIGH;
unsigned long tempoPressionado = 0;
bool segurando = false;

// ===== SERIAL =====
unsigned long ultimoPrint = 0;
const unsigned long intervaloPrint = 500;
float ultimoPeso = -999;

// ===== LIMITE =====
float LIMITE = 400.0;

// ===== ESTADO PARA O SITE =====
float pesoAtual  = 0;
bool  bombaLigada = false;
bool  dosando     = false;
float metaGramas  = 0;

// ===== LED (CATODO COMUM) =====
void setColor(bool r, bool g, bool b) {
  digitalWrite(LED_R, r);
  digitalWrite(LED_G, g);
  digitalWrite(LED_B, b);
}

// ===== CORS (necessário pois o site roda fora do ESP32) =====
void adicionarCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void tratarOptions() {
  adicionarCORS();
  server.send(204);
}

// ===== HANDLERS DA API =====
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
  balanca.tare();      // zera a balança antes de iniciar a dosagem
  ultimoPeso = -999;
  dosando = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void tratarTara() {
  adicionarCORS();
  Serial.println(">>> TARA (via site) <<<");
  balanca.tare();
  ultimoPeso = -999;
  setColor(0,1,0); // 🟢 verde, igual à tara pelo botão físico
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

  pinMode(BOTAO, INPUT_PULLUP);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  pinMode(PINO_BOMBA, OUTPUT);
  digitalWrite(PINO_BOMBA, LOW);

  servoDosador.attach(PINO_SERVO);
  servoDosador.write(0);

  balanca.begin(DT, SCK);
  balanca.set_scale(fator);
  balanca.tare();

  Serial.println("=== SISTEMA PRONTO ===");
  Serial.println("Clique = TARA");
  Serial.println("Segurar 2s = CALIBRAR (1kg)");

  setColor(0,0,0);

  // ===== WiFi =====
  WiFi.begin(ssid, senha);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP do ESP32: ");
  Serial.println(WiFi.localIP());   // <-- use esse IP no site

  // ===== Rotas da API =====
  server.on("/api/status", HTTP_GET,     tratarStatus);
  server.on("/api/status", HTTP_OPTIONS, tratarOptions);
  server.on("/api/bomba",  HTTP_POST,    tratarBomba);
  server.on("/api/bomba",  HTTP_OPTIONS, tratarOptions);
  server.on("/api/servo",  HTTP_POST,    tratarServo);
  server.on("/api/servo",  HTTP_OPTIONS, tratarOptions);
  server.on("/api/dosar",  HTTP_POST,    tratarDosar);
  server.on("/api/dosar",  HTTP_OPTIONS, tratarOptions);
  server.on("/api/tara",   HTTP_POST,    tratarTara);
  server.on("/api/tara",   HTTP_OPTIONS, tratarOptions);
  server.on("/api/parar",  HTTP_POST,    tratarParar);
  server.on("/api/parar",  HTTP_OPTIONS, tratarOptions);

  server.begin();
}

void loop() {
  server.handleClient();

  bool estado = digitalRead(BOTAO);

  // ===== PRESSIONOU =====
  if (estado == LOW && estadoAnt == HIGH) {
    tempoPressionado = millis();
    segurando = true;
  }

  // ===== SOLTOU =====
  if (estado == HIGH && estadoAnt == LOW) {

    unsigned long tempo = millis() - tempoPressionado;

    // clique curto = TARA
    if (tempo < 2000) {
      Serial.println(">>> TARA <<<");
      balanca.tare();
      ultimoPeso = -999;

      setColor(0,1,0); // 🟢 verde
    }

    segurando = false;
  }

  // ===== SEGURANDO = CALIBRAR =====
  if (estado == LOW && segurando) {
    if (millis() - tempoPressionado > 2000) {

      Serial.println(">>> CALIBRACAO <<<");
      Serial.println("Coloque 1kg...");

      delay(5000);

      long leitura = balanca.read_average(50);

      fator = leitura / 1000.0;
      balanca.set_scale(fator);

      calibrado = true;

      Serial.print("Novo fator: ");
      Serial.println(fator);

      setColor(1,1,1); // branco = calibrou
      delay(1000);

      segurando = false;
    }
  }

  estadoAnt = estado;

  // ===== LEITURA =====
  if (balanca.is_ready()) {

    float peso = balanca.get_units(10);

    if (fabs(peso) < 5) peso = 0;

    pesoAtual = peso;

    if (millis() - ultimoPrint > intervaloPrint) {

      if (fabs(peso - ultimoPeso) >= 1) {
        Serial.print("Peso (g): ");
        Serial.println(peso);
        ultimoPeso = peso;
      }

      ultimoPrint = millis();
    }

    // ===== LED =====
    // Enquanto está dosando, o LED indica isso; fora disso segue a lógica original.
    if (dosando) {
      setColor(0,0,1); // 🔵 dosando
    }
    else if (peso >= LIMITE) {
      setColor(1,0,0); // 🔴
    }
    else if (peso > 5) {
      setColor(0,0,1); // 🔵
    }
    else {
      setColor(0,1,0); // 🟢
    }
  }

  // ===== DOSAGEM AUTOMÁTICA (via site) =====
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
