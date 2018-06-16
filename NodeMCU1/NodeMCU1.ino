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

uint8_t MACslave[6] = {0xDC, 0x4F, 0x22, 0x18, 0x20, 0x6E}; //Ângela

#define ROLE 2
#define CHANNEL 3

//Variáveis acumuladoras para enviar ao ThingSpeak
int umidade = 0, temperatura = 0, indCalor = 0, reEnvioMsg = 0;
int qtdEnviosUmidade = 0, qtdEnviosTemperatura = 0, qtdEnviosIndCalor = 0;
uint32_t lastEnvio = 0;

//Definições serviços em nuvem
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define SSID "LUCAS"
#define PSWD "889161639397"

String API_KEY_TS = "XXCA1UN2OS46BDVI";
const char* SERVER_TS = "api.thingspeak.com";

const char* SERVER_PB = "api.pushbullet.com";
const int PORT_PB = 443;
const char* API_KEY_PB = "o.RVnAlibsp0K9ikkqjY9GfBEOEHpMvBbL";

const char* SERVER_MQTT = "m12.cloudmqtt.com";
const int PORT_MQTT = 18459;
const char* USER_MQTT = "excuhodm";
const char* PSWD_MQTT = "EDqD2QOcCL3W";

WiFiClient ESPClient;
PubSubClient MQTT(ESPClient);

//Métodos uteis para o protocolo ESP Now
void IniciaESPNow() { //Inicia ESP Now
  if (esp_now_init() != 0) {
    Serial.println("Falha ao iniciar protocolo ESP-NOW; Reiniciando...");
    ESP.restart();
    delay(1);
  }

  Serial.print("AP MAC: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.print("STA MAC: ");
  Serial.println(WiFi.macAddress());

  esp_now_set_self_role(3); //Iniciando no modo MASTER+SLAVE

  uint8_t KEY[0] = {};
  //uint8_t key[16] = {0,255,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
  uint8_t KEY_LEN = sizeof(KEY);

  esp_now_add_peer(MACslave, ROLE, CHANNEL, KEY, KEY_LEN); //Pareando ao escravo
}

void Envia(uint16_t valor) { //Envia ao dispositivo final o sinal de ligar ou desligar o motor
  DADOS dados;
  String topico = "motor";
  topico.toCharArray(dados.topico, sizeof(dados.topico));
  dados.valor = valor;

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
  if(status == 0){
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
  }else{
    delay(2);
    Envia(reEnvioMsg);//Renvia mensagem caso não recebeu
  }
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

  char val[5];
  sprintf(val, "%d", dados.valor);

  MQTT.publish(dados.topico, val); //envia para o MQTT

  //Acumula os valores recebidos, para mandar a média ao ThingSpeak
  if (String(dados.topico).equals("umidade")) {
    umidade += dados.valor;
    qtdEnviosUmidade++;
  } else if (String(dados.topico).equals("temperatura")) {
    temperatura += dados.valor;
    qtdEnviosTemperatura++;
  } else if (String(dados.topico).equals("heat_index")) {
    indCalor += dados.valor;
    qtdEnviosIndCalor++;
  }
}

//Método de notificação
void Notificar(String titulo, String corpo) { //Envia notificação ao celular
  WiFiClientSecure PushBullet;
  Serial.print("Conectando a ");
  Serial.println(SERVER_PB);
  if (!PushBullet.connect(SERVER_PB, PORT_PB)) {
    Serial.println("Conexão ao PushBullet Falhou!");
    return;
  }

  const String URL = "/v2/pushes";
  String MSG = "{\"type\": \"note\", \"title\": \"" + titulo + "\", \"body\": \"" + corpo + "\"}\r\n";

  PushBullet.print(String("POST ") + URL + " HTTP/1.1\r\n" +
                   "Host: " + SERVER_PB + "\r\n" +
                   "Authorization: Bearer " + API_KEY_PB + "\r\n" +
                   "Content-Type: application/json\r\n" +
                   "Content-Length: " +
                   String(MSG.length()) + "\r\n\r\n");
  PushBullet.print(MSG);

  Serial.println("Requisicao enviada, verifique o celular.");

}

//Método de inicialização do Wi-Fi
void IniciaWiFi() {
  Serial.print("Conectando a ");
  Serial.println(SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PSWD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Gateway:");
  Serial.println(WiFi.gatewayIP());
  Serial.print("AP MAC:");
  Serial.println(WiFi.BSSIDstr());
}

//Método de inicialização do MQTT
void IniciaMQTT() {
  MQTT.setServer(SERVER_MQTT, PORT_MQTT);
  MQTT.setCallback(MQTT_Callback);

  while (!MQTT.connected()) {
    Serial.println("Conectando ao MQTT...");

    if (MQTT.connect("ESP8266Client", USER_MQTT, PSWD_MQTT )) {
      Serial.println("Conectado ao MQTT!");
    } else {
      Serial.print("Falha ao se conectar ao MQTT com estado: ");
      Serial.println(MQTT.state());
      delay(2000);
    }
  }

  //Primeiro publish e se inscrevendo nos tópicos que seram usados
  MQTT.publish("Iniciou", "OK");
  MQTT.subscribe("motor");

}

void MQTT_Callback(char* topic, byte* payload, unsigned int length) {
  digitalWrite(LED_BUILTIN, LOW);
  Serial.print("Mensagem recebida no topico: ");
  Serial.print(topic);
  Serial.print(" Mensagem:");
  String msg;
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    char c = (char)payload[i];
    msg += c;
  }
  Serial.println();
  Serial.println("-----------------------------");

  String topico = String(topic);

  if (topico.equals("motor")) { //Envia para o nodeMCU 2 a mensagem de ligar ou desligar o motor
    reEnvioMsg = msg.toInt();
    Envia(msg.toInt());
  }
  Serial.println("-----------------------------");
}

//Método de envio dos dados para o ThingSpeak
void EnviaThingSpeak(int t, int h, int hic) {
  WiFiClient Client;
  if (Client.connect(SERVER_TS, 80)) {
    String postStr = API_KEY_TS;
    postStr += "&field1=";
    postStr += String(t);
    postStr += "&field2=";
    postStr += String(h);
    postStr += "&field3=";
    postStr += String(hic);
    postStr += "\r\n\r\n";

    Client.print("POST /update HTTP/1.1\n");
    Client.print("Host: " + String(SERVER_TS) + "\n");
    Client.print("Connection: close\n");
    Client.print("X-THINGSPEAKAPIKEY: " + API_KEY_TS + "\n");
    Client.print("Content-Type: application/x-www-form-urlencoded\n");
    Client.print("Content-Length: ");
    Client.print(postStr.length());
    Client.print("\n\n");
    Client.print(postStr);

    Serial.print("Temperatura média: ");
    Serial.print(t);
    Serial.print(" *C, Umidade média: ");
    Serial.print(h);
    Serial.print(" %, Indice de Calor: ");
    Serial.print(hic);
    Serial.println(" *C. Enviado ao Thingspeak.");
  }
  Client.stop();

  Serial.println("Aguardando...");
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  Serial.println("----- INICIANDO PROTOCOLO Wi-Fi -----");
  IniciaWiFi();
  Serial.println("----- PRONTO -----");

  Serial.println("----- INICIANDO PROTOCOLO MQTT -----");
  IniciaMQTT();
  Serial.println("----- PRONTO -----");

  Serial.println("----- INICIANDO PROTOCOLO ESP-NOW -----");
  IniciaESPNow();
  Serial.println("----- PRONTO -----");

  //Callbacks de envio e recebimento do ESP NOW
  esp_now_register_send_cb(Enviou);
  esp_now_register_recv_cb(Recebeu);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  MQTT.loop();
  if ((millis() - lastEnvio) >= 20000) { //Não pode usar delay
    lastEnvio = millis();
    if(qtdEnviosTemperatura > 0 && qtdEnviosUmidade > 0 && qtdEnviosIndCalor > 0){
      EnviaThingSpeak(temperatura / qtdEnviosTemperatura, umidade / qtdEnviosUmidade, indCalor / qtdEnviosIndCalor);
      umidade = temperatura = indCalor = qtdEnviosIndCalor = qtdEnviosTemperatura = qtdEnviosUmidade = 0;
      Serial.println("Enviou");
    }
  }
}
