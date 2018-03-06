/*################################################################################## RTC ##################################################################################*/
// Carrega a biblioteca virtuabotixRTC
#include <virtuabotixRTC.h> 

// Determina os pinos ligados ao modulo
// myRTC(clock, data, rst)
virtuabotixRTC myRTC(D6, D7, D8);


/*################################################################################## "NTP" em Webservice ##################################################################################*/
// Servidor com o webservice da data.
const char* data_hora_server = "http://server.xxx.xx/get_data.php";
#include <ESP8266HTTPClient.h>
#include <DHT.h>


// Conexao wifi
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

char * MY_SSID;
char * MY_PWD;

const char *ssid = "AguIno";
const char *password = "";


#include "FS.h"

#define RELAY1LIGA "relay1liga.txt"
#define RELAY1DESLIGA "relay1desliga.txt"
#define RELAY1WEEKDAYS "weekdays.txt"
#define WIFIINFO "wifiinfo.txt"
#define UMIDADE "umidade.txt"

struct Agendamento {
  int horaliga;
  int minutoliga;
  int horadesliga;
  int minutodesliga;
  boolean weekday_dom;
  boolean weekday_2a;
  boolean weekday_3a;
  boolean weekday_4a;
  boolean weekday_5a;
  boolean weekday_6a;
  boolean weekday_sab;
};

boolean isAgendado = false;
boolean isAutomaticMode = false;
int umidade_minima_solo = 0;

int contador = 0;


#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
ESP8266WebServer server(80);


#define RELE1 D1

#define DHTPIN D2
#define DHTTYPE DHT22 // DHT 22
#define myPeriodic 60 //in sec | Thingspeak pub is 60sec

#define SENSOR_SOLO A0 // Define o pino que lera a saída do sensor do solo

unsigned long channelNumber = 130178;

WiFiClient client;
DHT dht(DHTPIN, DHTTYPE);

Agendamento a;

void setup()
{    
  Serial.begin(115200);
  dht.begin();
  SPIFFS.begin();
      
  // Seta o relê como saida.. DESLIGADA POR PADRÃO
  pinMode(RELE1, OUTPUT);
  digitalWrite(RELE1, HIGH);
     
  //ThingSpeak.begin(client);


  if (readWifiFromSPIFFS()) {
    //WiFi.mode(WIFI_STA);// station mode
    Serial.printf("Wi-Fi mode set to WIFI_STA %s\n", WiFi.mode(WIFI_STA) ? "OK" : "Failed!");
 
   
    if (connectWifi())  {
      if (MDNS.begin("esp8266")) {
        Serial.println("MDNS responder started");
      }

      // Consulta o "NTP" para acertar a data do módulo RTC
      setDataRTC();

      // Paths que o Aguino vai responder.
      server.on("/format", [](){
        printRequestSerial();
        SPIFFS.format();
        server.send(200, "text/plain", "OK");
      }); 
    
      server.on("/", handleRoot);
      server.onNotFound(handleNotFound);
    
      server.on("/agendamento/1", [](){
        printRequestSerial();
        checarAgendamento();
        server.send(200, "text/plain", "ok");
      });
    
      server.on("/agendamento/0", [](){
        printRequestSerial();
        isAgendado = false;
        Serial.println("Agendamento cancelado!");
        server.send(200, "text/plain", String());
      });
    
      // Switch virtual.
      server.on("/gpio/1", [](){
        isAgendado = false;
        isAutomaticMode = false;
        Serial.println("Agendamento cancelado!");
        digitalWrite(RELE1, LOW);
        printRequestSerial();
        server.send(200, "text/plain", "1");
      });
      server.on("/gpio/0", [](){
        isAgendado = false;
        isAutomaticMode = false;
        Serial.println("Agendamento cancelado!");
        digitalWrite(RELE1, HIGH);
        printRequestSerial();
        server.send(200, "text/plain", "0");
      });
    
       // Switch virtual.
      server.on("/automatico/1", [](){
        isAutomaticMode = true;
        Serial.println("Automatic mode ON!");
        printRequestSerial();
        server.send(200, "text/plain", "1");
      });
      server.on("/automatico/0", [](){
        isAutomaticMode = false;
        Serial.println("Automatic mode OFF!");
        printRequestSerial();
        server.send(200, "text/plain", "0");
      });
    
      // Método que receberá os horários de agendamento
      server.on("/agendamento", gravarSPIFFS);
      server.on("/getagendamento", ajaxGetAgendamentoPage);
      server.on("/wifi", gravarSPIFFSWifi);
      server.on("/config", wifiSetterPage);
   
      server.begin();
    }
    else {
     connectWifiAP();
    }
  }
  else {
    Serial.println("Configurar WiFi!");
    connectWifiAP();
  }

}

void loop()
{
   myRTC.updateTime();
 
  server.handleClient();

//  delay(1000);
  
  // Se estiver agendado, devemos ver se está na hora de ligar/desligar..
  if (isAgendado) {
    if (isDayOfWeek(myRTC.dayofweek, a)) {
      if (myRTC.hours == a.horaliga && myRTC.minutes == a.minutoliga && myRTC.seconds == 0 && digitalRead(RELE1) == HIGH) {
        Serial.print("Ligando Rele agendado as: ");
        Serial.print(a.horaliga);
        Serial.print(":");
        Serial.println(a.minutoliga);
        digitalWrite(RELE1, LOW);
        delay(500);
      }
      else if(myRTC.hours == a.horadesliga && myRTC.minutes == a.minutodesliga && myRTC.seconds == 1 && digitalRead(RELE1) == LOW) {
        Serial.print("Desligando Rele agendado as: ");
        Serial.print(a.horadesliga);
        Serial.print(":");
        Serial.println(a.minutodesliga);
        digitalWrite(RELE1, HIGH);
        delay(500);
      }
    }
  // LIGAR e DESLIGAR AUTOMATICAMENTE CASO UMIDADE MINIMA SEJA ATINGIDA 
  } else if (isAutomaticMode && contador > 5000){
      contador = 0;

      float solo = getUmidadeSolo();
      if(solo > 0 && solo < umidade_minima_solo && digitalRead(RELE1) == HIGH){
          Serial.print("Ligando Rele umidade minima atingida as: ");
          Serial.print(myRTC.hours);
          Serial.print(":");
          Serial.print(myRTC.minutes);
          Serial.print(":");
          Serial.println(myRTC.seconds);
          digitalWrite(RELE1, LOW);
          delay(500);
      } else if (solo > umidade_minima_solo && digitalRead(RELE1) == LOW){
          Serial.print("Desligando Rele umidade minima ultrapassada as: ");
          Serial.print(myRTC.hours);
          Serial.print(":");
          Serial.print(myRTC.minutes);
          Serial.print(":");
          Serial.println(myRTC.seconds);
          digitalWrite(RELE1, HIGH);
          delay(500);
      }
  }
  if (isAutomaticMode) {
    contador++;  
  }
}



/*###################################################################################################### AGENDAMENTO ######################################################################################################*/

void checarAgendamento() {
  if (readAgendamentoFromSPIFFS()) {
    Serial.print("Agendando para horários: ");
    Serial.print("Ligar as: ");
    Serial.print(a.horaliga);
    Serial.print(":");
    Serial.print(a.minutoliga);
    Serial.print(" - Desligar as:");
    Serial.print(a.horadesliga);
    Serial.print(":");
    Serial.print(a.minutodesliga); 
    Serial.print(" - Dias:"); 
    if (a.weekday_dom) Serial.print(" Domingos "); 
    if (a.weekday_2a) Serial.print(" Segundas "); 
    if (a.weekday_3a) Serial.print(" Terças "); 
    if (a.weekday_4a) Serial.print(" Quartas "); 
    if (a.weekday_5a) Serial.print(" Quintas "); 
    if (a.weekday_6a) Serial.print(" Sextas "); 
    if (a.weekday_sab) Serial.print(" Sábados "); 
    isAgendado = true;
  }
}


/*###################################################################################################### GRAVAR/LER ARQUIVOS AGENDAMENTO... ######################################################################################################*/

void gravarSPIFFS() {
  if (server.hasArg("relay1_liga") && server.hasArg("relay1_desliga")) {
    printRequestSerial();
    String line1 = server.arg("relay1_liga");
    String line2 = server.arg("relay1_desliga");

    String weekdays = "";

    if (server.hasArg("weekday-dom") && server.arg("weekday-dom")) {
      weekdays += "1";
    }
    if (server.hasArg("weekday-2a") && server.arg("weekday-2a")) {
      weekdays += "2";
    }
    if (server.hasArg("weekday-3a") && server.arg("weekday-3a")) {
      weekdays += "3";
    }
    if (server.hasArg("weekday-4a") && server.arg("weekday-4a")) {
      weekdays += "4";
    }
    if (server.hasArg("weekday-5a") && server.arg("weekday-5a")) {
      weekdays += "5";
    }
    if (server.hasArg("weekday-6a") && server.arg("weekday-6a")) {
      weekdays += "6";
    }
    if (server.hasArg("weekday-sab") && server.arg("weekday-sab")) {
      weekdays += "7";
    }    
    
    File f = SPIFFS.open(RELAY1WEEKDAYS, "w");
    if (!f) {
        Serial.println("file open failed");
    }
    // Grava o que leu do form no campo liga.
    f.println(weekdays);
    f.close();

    f = SPIFFS.open(RELAY1LIGA, "w");
    if (!f) {
        Serial.println("file open failed");
    }
    // Grava o que leu do form no campo liga.
    f.println(line1);
    f.close();

    f = SPIFFS.open(RELAY1DESLIGA, "w");
    if (!f) {
        Serial.println("file open failed");
    }
    // Grava o que leu do form no campo liga.
    f.println(line2);
    f.close();
    Serial.print("Resposta: ");

    String response = "";
    response.concat("<script type=\"text/javascript\">alert(\"Dados gravados: ");
    response.concat(line1);
    response.concat(" - ");
    response.concat(line2);
    response.concat(" - ");
    response.concat(weekdays);
    response.concat("\");");
    response.concat("window.location.href = \"/\";");
    response.concat("</script>");

    Serial.println(response);

    server.send(200, "text/html", response);
  }
}


// Função que irá ler os agendamentos do SPIFFS, retorna 0 se ocorrer algum erro ao ler algum dos arquivos, ou 1 se a leitora foi ok.
boolean readAgendamentoFromSPIFFS() {
  // Obj do Arquivo
  File f;
 
  // Se o arquivo existe, vamos tentar abrir ele para leitura..
  if (SPIFFS.exists(RELAY1LIGA)) {
    f = SPIFFS.open(RELAY1LIGA, "r");
    if (!f) {
        Serial.println("file relay1liga open failed");
        return false;
    }
    String s = f.readStringUntil('\n');
    a.horaliga = s.substring(0,2).toInt();
    a.minutoliga = s.substring(3,5).toInt();
    f.close();
  }

  // Se o arquivo existe, vamos tentar abrir ele para leitura..
  if (SPIFFS.exists(RELAY1DESLIGA)) {
    f = SPIFFS.open(RELAY1DESLIGA, "r");
    if (!f) {
        Serial.println("file relay1desliga open failed");
        return false;
    }
    String s = f.readStringUntil('\n');
    a.horadesliga = s.substring(0,2).toInt();
    a.minutodesliga = s.substring(3,5).toInt();
    f.close();
  }

  // Se o arquivo existe, vamos tentar abrir ele para leitura..
  if (SPIFFS.exists(RELAY1WEEKDAYS)) {
    f = SPIFFS.open(RELAY1WEEKDAYS, "r");
    if (!f) {
        Serial.println("file RELAY1WEEKDAYS open failed");
        return false;
    }
    String s = f.readStringUntil('\n');

    a.weekday_dom = false;
    a.weekday_2a = false;
    a.weekday_3a = false;
    a.weekday_4a = false;
    a.weekday_5a = false;
    a.weekday_6a = false;
    a.weekday_sab = false;
  
    for (int i = 0; i < s.length(); i++) {
      switch(s.charAt(i)) {
        case '1':
          a.weekday_dom = true;
          break;
        case '2':
          a.weekday_2a = true;
          break;
        case '3':
          a.weekday_3a = true;
          break;
        case '4':
          a.weekday_4a = true;
          break;
        case '5':
          a.weekday_5a = true;
          break;
        case '6':
          a.weekday_6a = true;
          break;
        case '7':
          a.weekday_sab = true;
          break;
      }
    }
    f.close();
  }
  
  Serial.println("Lido agendamento da memoria com sucesso! - ");
  return true;
}


/*############################################################################################ GRAVAR/LER ARQUIVOS WIFI ################################################################################################*/
// Função que irá gravar no SPIFFS os dados do wifi.
void gravarSPIFFSWifi() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.arg("ssid").length() > 0) {
    printRequestSerial();
    String line1 = server.arg("ssid");
    String line2 = server.arg("password");
    

    File f = SPIFFS.open(WIFIINFO, "w");
    if (!f) {
        Serial.println("file open failed");
    }
    // Grava o que leu do form no campo liga.
    f.println(line1);
    f.println(line2);
    f.close();

    Serial.print("Resposta: ");
    String response = "";
    response.concat("<script type=\"text/javascript\">alert(\"Dados gravados: SSID:");
    response.concat(line1);
    response.concat(" - PASSWD:");
    response.concat(line2);
    response.concat(". Aguarde um minuto para a AguIno reiniciar e se conectar ao wifi!\");");
    response.concat("window.location.href = \"/\";");
    response.concat("</script>");

    Serial.println(response);

    server.send(200, "text/html", response);

    ESP.restart();
  }
  else {
    String line3 = server.arg("umidade_minima_solo");
  
    File f = SPIFFS.open(UMIDADE, "w");
    if (!f) {
        Serial.println("file open failed");
    }
    // Grava o que leu do form no campo umidade minima solo.
    f.println(line3);
    f.close();


    Serial.print("Resposta: ");
    String response = "";
    response.concat("<script type=\"text/javascript\">alert(\"Nova umidade m&iacute;nima: ");
    response.concat(line3);
    response.concat("\");");
    response.concat("window.location.href = \"/\";");
    response.concat("</script>");

    Serial.println(response);
    umidade_minima_solo = line3.toInt();

    server.send(200, "text/html", response);
  }
}


// Função que irá ler os agendamentos do SPIFFS, retorna 0 se ocorrer algum erro ao ler algum dos arquivos, ou 1 se a leitora foi ok.
boolean readWifiFromSPIFFS() {
  // Obj do Arquivo
  File f;
 
  // Se o arquivo existe, vamos tentar abrir ele para leitura..
  if (SPIFFS.exists(WIFIINFO)) {
    f = SPIFFS.open(WIFIINFO, "r");
    if (!f) {
        Serial.println("file WIFI open failed");
        f.close();
        return false;
    }
    
    String ssid = f.readStringUntil('\n');
    MY_SSID = (char *) malloc(ssid.length());
    ssid.toCharArray(MY_SSID, ssid.length());
  
    String passwd = f.readStringUntil('\n');
    MY_PWD = (char *) malloc(passwd.length());
    passwd.toCharArray(MY_PWD, passwd.length());
    f.close();
    
     // Se o arquivo existe, vamos tentar abrir ele para leitura..
    if (SPIFFS.exists(UMIDADE)) {
      f = SPIFFS.open(UMIDADE, "r");
      if (!f) {
          Serial.println("file umidade open failed");
          f.close();
          return false;
      }
      String s = f.readStringUntil('\n');
      umidade_minima_solo = s.toInt();
      f.close();
    }

    Serial.println("Lido wifi ssid e umidade da memoria com sucesso!");
    return true;
  }
  else {
    return false;
  }
}




/*###################################################################################################### LEITURA SENSORES ######################################################################################################*/

//umidade do solo
float getUmidadeSolo(){
 
  float solo = (analogRead(SENSOR_SOLO));
  if (isnan(solo)){
    Serial.println("Failed to read from SOLO");
    return 0;
  }
  else
  {
    Serial.println("Umidade Solo: "+ String(solo));
    return solo;
  }
}

float getUmidadeAr(){
  // temperatura e umidade
  float h = dht.readHumidity();
 

  if (isnan(h))
  {
    Serial.println("Failed to read humidity from DHT");
    return 0;
  }
  else
  {
    Serial.println("Umidade Ar: "+ String(h));
    return h;
  }
}


float getTemperatura(){
  float t = dht.readTemperature();

  if (isnan(t))
  {
    Serial.println("Failed to read temperature from DHT");
    return 0;
  }
  else
  {
    Serial.println("Temperatura: "+ String(t));
    return t;
  }
}


/*###################################################################################################### PÁGINAS WEB ######################################################################################################*/

void handleRoot() {
  String message = "<!DOCTYPE html>";
  message += "<html>";
  message += "<head>";
  message += "    <title>AguIno INFO_PREF v1.1</title>";
  message += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  message += "    ";
  message += "      <link rel=\"stylesheet\" type=\"text/css\" href=\"http://code.jquery.com/mobile/1.4.2/jquery.mobile-1.4.2.min.css\">";
  message += "      <script type=\"text/javascript\" src=\"http://code.jquery.com/jquery-1.9.1.min.js\"></script>";
  message += "      <script type=\"text/javascript\" src=\"http://code.jquery.com/mobile/1.4.2/jquery.mobile-1.4.2.min.js\"></script>";
  message += "    <script>";
  message += "        $(document).bind(\"mobileinit\", function() {";
  message += "            $.mobile.ajaxEnabled = false;";
  message += "        });";
  message += "    </script>";
  message += "</head>";
  message += "<body>";
  message += "    <script type=\"text/javascript\">";
  message += "        $(document).ready(function() {";
  message += "            $('#switch').change(function() {";
  message += "                if ($(this).is(':checked')) {";
  message += "                    $.get(\"/gpio/1\");";
  message += "                } else {";
  message += "                    $.get(\"gpio/0\");";
  message += "                }";
  message += "                if ($('#switchAgenda').is(':checked')) { ";
  message += "                  $('#switchAgenda').attr('checked', false);";
  message += "                  $('#switchAgenda').flipswitch('refresh');";
  message += "                } if ($('#switchAutomatico').is(':checked')) { ";
  message += "                  $('#switchAutomatico').attr('checked', false);";
  message += "                  $('#switchAutomatico').flipswitch('refresh');";
  message += "                }";
  message += "            });";
  message += "            $('#switchAgenda').change(function() {";
  message += "                if ($(this).is(':checked')) {";
  message += "                    $.get(\"/agendamento/1\");";
  message += "                    $('#switchAutomatico').attr('checked', false);";
  message += "                    $('#switchAutomatico').flipswitch('refresh');";
  message += "                    $.get( \"getagendamento\", ";
  message += "                      function( data ) {";
  message += "                      var hora1 = parseInt(data.inicio.substring(0,2));";
  message += "                      var minuto1 = parseInt(data.inicio.substring(3,5));";
  message += "                      var hora2 = parseInt(data.fim.substring(0,2));";
  message += "                      var minuto2 = parseInt(data.fim.substring(3,5));";
  message += "                      $(\"#sliderTime1\").val(hora1 * 60 + minuto1);";
  message += "                      $('#sliderTime1').slider('refresh');";
  message += "                      $(\"#sliderTime2\").val(hora2 * 60 + minuto2);";
  message += "                      $('#sliderTime2').slider('refresh');";
  message += "                      $('#relay1_liga').val(data.inicio);";
  message += "                      $('#relay1_desliga').val(data.fim);";
  message += "                      if (data.dom) $(\"#weekday-dom\").click();";
  message += "                      if (data.seg) $(\"#weekday-2a\").click();";
  message += "                      if (data.ter) $(\"#weekday-3a\").click();";
  message += "                      if (data.qua) $(\"#weekday-4a\").click();";
  message += "                      if (data.qui) $(\"#weekday-5a\").click();";
  message += "                      if (data.sex) $(\"#weekday-6a\").click();";
  message += "                      if (data.sab) $(\"#weekday-sab\").click();";
  message += "                    });";
  message += "                } else {";
  message += "                    $.get(\"agendamento/0\");";
  message += "                }";
  message += "            });";
  message += "            $('#switchAutomatico').change(function() {";
  message += "                if ($(this).is(':checked')) {";
  message += "                    $.get(\"/automatico/1\");";
  message += "                    $('#switchAgenda').attr('checked', false);";
  message += "                    $('#switchAgenda').flipswitch('refresh');";
  message += "                } else {";
  message += "                    $.get(\"automatico/0\");";
  message += "                }";
  message += "            });"; 
  message += "            $(\"#sliderTime1\").on(\"change\", function(){";
  message += "                var time = IntToTime($(this).val());";
  message += "                $('#relay1_liga').val(time);";
  message += "            });";
  message += "            $(\"#sliderTime2\").on(\"change\", function(){";
  message += "                var time = IntToTime($(this).val());";
  message += "                $('#relay1_desliga').val(time);";
  message += "            });";
  message += "            function IntToTime(val){";
  message += "                var hours = parseInt( val / 60 );";
  message += "                var min = val - (hours * 60);";
  message += "                var time = (hours < 10 ? '0' + hours : hours) + ':' + (min < 10 ? '0' + min : min);";
  message += "                return time;";
  message += "            }";
  message += "            function TimeToInt(val){";
  message += "                var hours = parseInt(val.split(\":\")[0]) * 60;";
  message += "                var min = parseInt(val.split(\":\")[1]);";
  message += "                var time = (hours + min);";
  message += "                return time;";
  message += "            }";
  message += "        });";
  message += "    </script>";
  message += "    <div data-role=\"page\">";
  message += "        <div data-role=\"main\" class=\"ui-content\"> <label><font size=\"4\" color=\"blue\">AguIno Info Pref v1.1</font>";
  message += ("<br><b>Data - Hora:</b> ");
  // Chama a rotina que imprime o dia da semana
  message += (myRTC.dayofmonth);
  message += ("/");
  message += (myRTC.month);
  message += ("/");
  message += (myRTC.year);
  message += (" - ");
  // Adiciona um 0 caso o valor da hora seja <10
  if (myRTC.hours < 10) message += ("0");
  message += (myRTC.hours);
  message += (":");
  // Adiciona um 0 caso o valor dos minutos seja <10
  if (myRTC.minutes < 10) message += ("0");
  message += (myRTC.minutes);
  message += (":");
  // Adiciona um 0 caso o valor dos segundos seja <10
  if (myRTC.seconds < 10) message += ("0");
  message += (myRTC.seconds);

  message += ("<br> <b>Umidade do solo:</b> " + String(getUmidadeSolo()));
  message += ("<br> <b>Temperatura:</b> " + String(getTemperatura()) + "<b> Umidade: </b>" + String(getUmidadeAr()) );

  message += "</label>";
  if (digitalRead(RELE1) == LOW) {
    message += "    <label for=\"switch\">On/Off: </label><input type=\"checkbox\" data-role=\"flipswitch\" name=\"switch\" id=\"switch\" checked>";
  }
  else {
    message += "    <label for=\"switch\">On/Off: </label><input type=\"checkbox\" data-role=\"flipswitch\" name=\"switch\" id=\"switch\" >";
  }
  if (isAgendado) {
    message += "    <label for=\"switchAgenda\">Agendamento: </label><input type=\"checkbox\" data-role=\"flipswitch\" name=\"switchAgenda\" id=\"switchAgenda\" checked>";
  }
  else {
    message += "    <label for=\"switchAgenda\">Agendamento: </label><input type=\"checkbox\" data-role=\"flipswitch\" name=\"switchAgenda\" id=\"switchAgenda\" >";
  }
  if (isAutomaticMode) {
    message += "    <label for=\"switchAutomatico\">Autom&aacute;tico: </label><input type=\"checkbox\" data-role=\"flipswitch\" name=\"switchAutomatico\" id=\"switchAutomatico\" checked>";
  }
  else {
    message += "    <label for=\"switchAutomatico\">Autom&aacute;tico: </label><input type=\"checkbox\" data-role=\"flipswitch\" name=\"switchAutomatico\" id=\"switchAutomatico\" >";
  }
 
  message += "            <form action=\"/agendamento\" method=\"post\" data-ajax=\"false\">";
  message += "                <label for=\"sliderTime1\">Ligar &#224;s: </label>";
  if (isAgendado) {
    readAgendamentoFromSPIFFS();
    message += "                <input type=\"range\" name=\"sliderTime1\" id=\"sliderTime1\" data-highlight=\"true\" min=\"0\" max=\"1439\" value=\""; message += calculaValorSlider(a.horaliga, a.minutoliga); message += "\" step=\"5\" style=\"display: none;\"/>";
    message += "                <input type=\"text\" name=\"relay1_liga\" id=\"relay1_liga\" value=\""; if (a.horaliga < 10) { message += "0"; } message += String(a.horaliga); message += ":";  if (a.minutoliga < 10) { message += "0"; } message += String(a.minutoliga); message+= "\">";
    message += "                <br/>";
    message += "                <label for=\"sliderTime2\">Desligar &#224;s: </label>";
    message += "                <input type=\"range\" name=\"sliderTime2\" id=\"sliderTime2\" data-highlight=\"true\" min=\"0\" max=\"1439\" value=\""; message += calculaValorSlider(a.horadesliga, a.minutodesliga); message += "\" step=\"5\" style=\"display: none;\"/>";
    message += "                <input type=\"text\" name=\"relay1_desliga\" id=\"relay1_desliga\" value=\""; if (a.horadesliga < 10) { message += "0"; } message += String(a.horadesliga); message += ":"; if (a.minutodesliga < 10) { message += "0"; } message += String(a.minutodesliga); message+= "\">";
  }
  else {
    message += "                <input type=\"range\" name=\"sliderTime1\" id=\"sliderTime1\" data-highlight=\"true\" min=\"0\" max=\"1439\" value=\"800\" step=\"5\" style=\"display: none;\"/>";
    message += "                <input type=\"text\" name=\"relay1_liga\" id=\"relay1_liga\">";
    message += "                <br/>";
    message += "                <label for=\"sliderTime2\">Desligar &#224;s: </label>";
    message += "                <input type=\"range\" name=\"sliderTime2\" id=\"sliderTime2\" data-highlight=\"true\" min=\"0\" max=\"1439\" value=\"1000\" step=\"5\" style=\"display: none;\"/>";
    message += "                <input type=\"text\" name=\"relay1_desliga\" id=\"relay1_desliga\">";
  }
  

  if (isAgendado) {
    message += "<fieldset data-role=\"controlgroup\" data-type=\"horizontal\">";
    message += "  <legend>Dias da semana:</legend>";
    message += "  <input name=\"weekday-dom\" id=\"weekday-dom\" type=\"checkbox\""; if (a.weekday_dom) { message += " checked "; } message += ">";
    message += "  <label for=\"weekday-dom\">Domingo</label>";
    message += "  <input name=\"weekday-2a\" id=\"weekday-2a\" type=\"checkbox\""; if (a.weekday_2a) { message += " checked "; } message += ">";
    message += "  <label for=\"weekday-2a\">Segunda</label>";
    message += "  <input name=\"weekday-3a\" id=\"weekday-3a\" type=\"checkbox\""; if (a.weekday_3a) { message += " checked "; } message += ">";
    message += "  <label for=\"weekday-3a\">Ter&ccedil;a</label>";
    message += "  <input name=\"weekday-4a\" id=\"weekday-4a\" type=\"checkbox\""; if (a.weekday_4a) { message += " checked "; } message += ">";
    message += "  <label for=\"weekday-4a\">Quarta</label>";
    message += "  <input name=\"weekday-5a\" id=\"weekday-5a\" type=\"checkbox\""; if (a.weekday_5a) { message += " checked "; } message += ">";
    message += "  <label for=\"weekday-5a\">Quinta</label>";
    message += "  <input name=\"weekday-6a\" id=\"weekday-6a\" type=\"checkbox\""; if (a.weekday_6a) { message += " checked "; } message += ">";
    message += "  <label for=\"weekday-6a\">Sexta</label>";
    message += "  <input name=\"weekday-sab\" id=\"weekday-sab\" type=\"checkbox\""; if (a.weekday_sab) { message += " checked "; } message += ">";
    message += "  <label for=\"weekday-sab\">S&aacute;bado</label>";
    message += "</fieldset>";
  }
  else {
    message += "<fieldset data-role=\"controlgroup\" data-type=\"horizontal\">";
    message += "  <legend>Dias da semana:</legend>";
    message += "  <input name=\"weekday-dom\" id=\"weekday-dom\" type=\"checkbox\">";
    message += "  <label for=\"weekday-dom\">Domingo</label>";
    message += "  <input name=\"weekday-2a\" id=\"weekday-2a\" type=\"checkbox\">";
    message += "  <label for=\"weekday-2a\">Segunda</label>";
    message += "  <input name=\"weekday-3a\" id=\"weekday-3a\" type=\"checkbox\">";
    message += "  <label for=\"weekday-3a\">Ter&ccedil;a</label>";
    message += "  <input name=\"weekday-4a\" id=\"weekday-4a\" type=\"checkbox\">";
    message += "  <label for=\"weekday-4a\">Quarta</label>";
    message += "  <input name=\"weekday-5a\" id=\"weekday-5a\" type=\"checkbox\">";
    message += "  <label for=\"weekday-5a\">Quinta</label>";
    message += "  <input name=\"weekday-6a\" id=\"weekday-6a\" type=\"checkbox\">";
    message += "  <label for=\"weekday-6a\">Sexta</label>";
    message += "  <input name=\"weekday-sab\" id=\"weekday-sab\" type=\"checkbox\">";
    message += "  <label for=\"weekday-sab\">S&aacute;bado</label>";
    message += "</fieldset>";
  }

  
  message += "                <p><input type=submit value=\"Agendar\"></p>";
  message += "            </form></div>";
  message += "    </div>";
  message += "</body>";
  message += "</html>";

  printRequestSerial();
  server.send(200, "text/html", message);
}

// Pagina para setar o wifi.
void wifiSetterPage() {
 String message = "<!DOCTYPE html>";
  message += "<html>";
  message += "<head>";
  message += "    <title>AguIno INFO_PREF v1.0 - Configurador de Wifi</title>";
  message += "</head>";
  message += "<body>";
  message += "<form action=\"/wifi\" method=\"post\">";
  message += "  <label for=\"ssid\">SSID: </label>";
  message += "  <input type=\"text\" name=\"ssid\" id=\"ssid\">";
  message += "  <br/>";
  message += "  <label for=\"password\">Senha: </label>";
  message += "  <input type=\"text\" name=\"password\" id=\"password\">";
  message += "  <br/>";
  message += "  <label for=\"umidade_minima_solo\">Umidade m&iacute;nima do solo: </label>";
  message += "  <input type=\"text\" name=\"umidade_minima_solo\" id=\"umidade_minima_solo\" value=\""; message += umidade_minima_solo; message +="\">";
  message += "  <p><input type=submit value=\"Gravar\"></p>";
  message += "</form>";
  message += "</body>";
  message += "</html>";

  printRequestSerial();
  server.send(200, "text/html", message);
}

void ajaxGetAgendamentoPage() {
  if (readAgendamentoFromSPIFFS()) {
    String message = "{\"inicio\":\""; if (a.horaliga < 10) {message += "0";}  message += a.horaliga; message += ":"; if (a.minutoliga < 10) {message += "0";}  message += a.minutoliga; 
    message += "\", \"fim\":\""; if (a.horadesliga < 10) {message += "0";} message += a.horadesliga; message += ":"; if (a.minutodesliga < 10) {message += "0";} message += a.minutodesliga; message += "\"";
    message += ", \"dom\":"; message += (a.weekday_dom? "true" : "false");
    message += ", \"seg\":"; message += (a.weekday_2a? "true" : "false");
    message += ", \"ter\":"; message += (a.weekday_3a? "true" : "false");
    message += ", \"qua\":"; message += (a.weekday_4a? "true" : "false");
    message += ", \"qui\":"; message += (a.weekday_5a? "true" : "false");
    message += ", \"sex\":"; message += (a.weekday_6a? "true" : "false");
    message += ", \"sab\":"; message += (a.weekday_sab? "true" : "false");
    message += "}";

    printRequestSerial();
    server.send(200, "application/json", message);
  }
  else {
    printRequestSerial();
    server.send(200, "text/html", "deu ruim");
  }
}

/*###################################################################################################### UTILS ######################################################################################################*/


void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  Serial.println(message);
  server.send(404, "text/plain", message);
}

int calculaValorSlider(int hora, int minuto) {
  return hora * 60 + minuto;
}

boolean connectWifi()
{
  int contador = 0;
  Serial.print("Conectando-se a ");
  Serial.print(MY_SSID);
  Serial.print(":");
  Serial.print(MY_PWD);
  WiFi.begin(MY_SSID, MY_PWD);
  while (contador < 30 && WiFi.status() != WL_CONNECTED) {
     delay(1000);
     Serial.print(".");
     contador++;
  }
  if (contador < 30) {
    Serial.println("");
    Serial.println("Conectado");
    Serial.println("");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    delay(500);
    return true;
  }
  else {
    Serial.println("Erro ao se conectar ao wifi");
    return false;
  }
}

void connectWifiAP() {
  Serial.println("Não foi possível conectar!");
  Serial.printf("Wi-Fi mode set to WIFI_AP %s\n", WiFi.mode(WIFI_AP) ? "OK" : "Failed!");

  WiFi.softAP(ssid, password); // Inicia o AP.
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  server.on("/", wifiSetterPage);
  server.on("/wifi", gravarSPIFFSWifi);
  server.on("/config", wifiSetterPage);
  server.onNotFound(handleNotFound);
  server.begin();
}

// Função que irá buscar a data no "NTP" da prefeitura em webservice, e setar ele no módulo RTC
void setDataRTC() {
    HTTPClient http;
    http.begin(data_hora_server);
    int httpCode = http.GET();

    if(httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf("Buscando data e hora do servidor ... ");
        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      
        if(httpCode == HTTP_CODE_OK) {
            String payload = http.getString();

            int dia = payload.substring(11, 13).toInt();
            int mes = payload.substring(14, 16).toInt();
            int ano = payload.substring(17, 21).toInt();
            int hora = payload.substring(6, 8).toInt();
            int minuto = payload.substring(3, 5).toInt();
            int segundo = payload.substring(0, 2).toInt();
            int dia_semana = payload.substring(9, 10).toInt()+1;

            switch(dia_semana) {
              case 2:
                Serial.print("Segunda-Feira, ");
                break;
              case 3:
                Serial.print("Terça-Feira, ");
                break;
              case 4:
                Serial.print("Quarta-Feira, ");
                break;
              case 5:
                Serial.print("Quinta-Feira, ");
                break;
              case 6:
                Serial.print("Sexta-Feira, ");
                break;
              case 7:
                Serial.print("Sábado, ");
                break;
              case 1:
                Serial.print("Domingo, ");
                break;
            }
            
            Serial.print(dia);
            Serial.print("/");
            Serial.print(mes);
            Serial.print("/");
            Serial.print(ano);
            Serial.print(" - ");
            Serial.print(hora);
            Serial.print(":");
            Serial.print(minuto);
            Serial.print(":");
            Serial.println(segundo);
           

            // Informacoes iniciais de data e hora
            // Apos setar as informacoes, comente a linha abaixo
            // (segundos, minutos, hora, dia da semana, dia do mes, mes, ano)
            myRTC.setDS1302Time(segundo, minuto, hora, dia_semana, dia, mes, ano);
        }
    }
    else {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
}


boolean isDayOfWeek(int dayofweek, Agendamento a) {
  if (dayofweek==1) {
    return a.weekday_dom;
  }
  if (dayofweek==2) {
    return a.weekday_2a;
  }
  if (dayofweek==3) {
    return a.weekday_3a;
  }
  if (dayofweek==4) {
    return a.weekday_4a;
  }
  if (dayofweek==5) {
    return a.weekday_5a;
  }
  if (dayofweek==6) {
    return a.weekday_6a;
  }
  if (dayofweek==7) {
    return a.weekday_sab;
  }
}

//Printa a request
void printRequestSerial() {
  Serial.print(server.uri());
  Serial.println((server.method() == HTTP_GET)?" - GET":" - POST");

  Serial.print("Arguments: ");
  Serial.println(server.args());
  for (uint8_t i=0; i<server.args(); i++){
    Serial.print(" " + server.argName(i) + ": " + server.arg(i) + "\n");
  }
  
}
