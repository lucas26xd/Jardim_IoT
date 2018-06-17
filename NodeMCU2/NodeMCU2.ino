#include <ESP8266WiFi.h>
// Definições do ESP NOW
extern "C" {
#include <espnow.h>
}

struct DADOS {
  char topico[12] = {};
  uint16_t valor = 0;
};

/*
   0xDC, 0x4F, 0x22, 0x18, 0x0D, 0x1F //Hugo
   0xDC, 0x4F, 0x22, 0x18, 0x22, 0x1D //Lucas
   0xDC, 0x4F, 0x22, 0x18, 0x20, 0x6E //Ângela
*/

uint8_t MACslave[6] = {0xDC, 0x4F, 0x22, 0x18, 0x22, 0x1D}; //Lucas

#define ROLE 3
#define CHANNEL 3

//Definições Sensores/Atuadores
#include<DHT.h>

const int D[] = {16, 5, 4, 0, 2, 14, 12, 13, 15, 3, 1}; //pinos NodeMCU

DHT dht(D[3], DHT11);

#define SOLO A0
#define MOTOR D[2]
boolean motorOn = false;

uint32_t lastEnvio = 0;

//Métodos uteis para o protocolo ESP Now
void IniciaESPNow() { //Inicia ESP Now
  if (esp_now_init() != 0) {
    Serial.println("Falha ao iniciar protocolo ESP-NOW, Reiniciando...");
    ESP.restart();
    delay(1);
  }

  Serial.print("AP MAC: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.print("STA MAC: ");
  Serial.println(WiFi.macAddress());

  esp_now_set_self_role(ROLE); //Iniciando no modo MASTER+SLAVE

  uint8_t KEY[0] = {};
  //uint8_t key[16] = {0,255,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
  uint8_t KEY_LEN = sizeof(KEY);

  esp_now_add_peer(MACslave, ROLE, CHANNEL, KEY, KEY_LEN); //Pareando ao escravo
}

void Envia(String topico) { //Envia uma estrutura de dados ao escravo pareado contando topico e valor a ser publicado
  DADOS dados;
  topico.toCharArray(dados.topico, sizeof(dados.topico));
  if (topico.equals("solo")) {
    dados.valor = (uint16_t) analogRead(A0)*100/750;
  } else if (topico. equals("umidade")) {
    if (isnan(dht.readHumidity())) {
      Serial.println("Falha na leitura da umidade do sensor!");
      return;
    }
    dados.valor = (uint16_t) dht.readHumidity();
  } else if (topico. equals("temperatura")) {
    if (isnan(dht.readTemperature())) {
      Serial.println("Falha na leitura da temperatura do sensor!");
      return;
    }
    dados.valor = (uint16_t) dht.readTemperature();
  } else if (topico. equals("heat_index")) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
        Serial.println("Falha ao ler o sensor!");
        return;
    }
    dados.valor = (uint16_t) dht.computeHeatIndex(t, h, false);
  } else if(topico. equals("motor")){//Mandar desligar o motor no topico do MQTT
    dados.valor = 0;
  } else {
    Serial.println("Nada a fazer!");
    dados.valor = 0;
    topico = "Nenhum";
    topico.toCharArray(dados.topico, sizeof(dados.topico));
  }

  Serial.print("Topico: ");
  Serial.print(topico);
  Serial.print(" Valor: ");
  Serial.println(dados.valor);

  uint8_t data[sizeof(dados)];
  memcpy(data, &dados, sizeof(dados));
  uint8_t len = sizeof(data);

  esp_now_send(MACslave, data, len); //envia dados para nodeMCU 2
}

void Enviou(uint8_t* mac, uint8_t status) { //Callback que verifica se foi recebido ACK
  char MACescravo[6];
  sprintf(MACescravo, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("Enviado p/ ESP de MAC: ");
  Serial.print(MACescravo);
  Serial.print(" Recepcao: ");
  Serial.println((status == 0 ? "OK" : "ERRO"));
}

void Recebeu(uint8_t *mac, uint8_t *data, uint8_t len) { //Callback chamado sempre que recebe um novo pacote
  char MACmestre[6];
  sprintf(MACmestre, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("Recepcao do ESP de MAC: ");
  Serial.println(MACmestre);

  DADOS dados;
  memcpy(&dados, data, sizeof(dados));

  Serial.print("Topico Recebido: ");
  Serial.print(dados.topico);
  Serial.print(" Valor Recebido: ");
  Serial.println(dados.valor);
  String topico = String(dados.topico);
  if (topico.equals("motor")) {
    digitalWrite(MOTOR, dados.valor);
    digitalWrite(LED_BUILTIN, !dados.valor);
    //delay(1000);
    //digitalWrite(MOTOR, LOW);
    //digitalWrite(LED_BUILTIN, HIGH);
  }
}

void desligaMotor(){
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(MOTOR, LOW);
  //Envia("motor");
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  dht.begin();

  IniciaESPNow();

  //Callbacks de envio e recebimento
  esp_now_register_send_cb(Enviou);
  esp_now_register_recv_cb(Recebeu);

  pinMode(MOTOR, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(MOTOR, LOW);
}

void loop() {
  if (millis() - lastEnvio >= 5000) { //Envia para o nodeMCU 1 os dados a cada 1 segundo
    lastEnvio = millis();
    Envia("solo");
    delay(2);
    Envia("temperatura");
    delay(2);
    Envia("umidade");
    delay(2);
    Envia("heat_index");
    delay(2);
  }
  if(analogRead(A0) > 480 && motorOn){//Desligamento para não enxarcar
    desligaMotor();
    motorOn = false;
  } else {
    motorOn = true;
  }
}

