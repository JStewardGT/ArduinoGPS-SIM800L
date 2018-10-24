/*
  SIM GPS Transmitter

  Simple project which logs data from GPS module (NEO 6M) into a web service using data HTTP GET requests through SIM800L module. 
  Location is sent for each interval given as configuration variable 'frequency'. 

  Connecting modules:
  Pin3 -> GPS-module-RX
  Pin4 -> GPS-module-TX
  Pin5 -> SIM-module-TX
  Pin6 -> SIM-module-RX
  
  Dependency(TinyGPS++ library): http://arduiniana.org/libraries/tinygpsplus/
  
  created   Jul 2017
  by CheapskateProjects

  ---------------------------
  The MIT License (MIT)

  Copyright (c) 2017 CheapskateProjects

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <SoftwareSerial.h>
#include <TinyGPS++.h>

// Config (Use APN correspondiente a las configuraciones de sus proveedores de servicios)
static String apn="internet";
static String loggingPassword="qwerty";
static String serverIP="1.2.3.4";

// Pines donde se conectan los módulos GPS y SIM
static const int SimRXPin = 5, SimTXPin = 6;
static const int GPSRXPin = 4, GPSTXPin = 3;
static const int ErrorPin = 10, SimConnectionPin = 12;

// Velocidad en baudios utilizados (definición basada en módulos usados)
static const uint32_t SimBaudrate = 9600;
static const uint32_t GPSBaud = 9600;
static const uint32_t SerialBaudrate = 9600;

// Con qué frecuencia queremos enviar la ubicación (milisegundos)
static const unsigned long frequency = 15000;

// Tiempo máximo para esperar módulo SIM para respuesta
static long maxResponseTime = 30000;

String responseString;
TinyGPSPlus gps;
unsigned long previous=0;
unsigned long beltTime;
SoftwareSerial sim_ss(SimRXPin, SimTXPin);
SoftwareSerial gps_ss(GPSRXPin, GPSTXPin);

void setup()
{
  // Inicializar pines de estado
  pinMode(ErrorPin, OUTPUT);
  pinMode(SimConnectionPin, OUTPUT);
  digitalWrite(ErrorPin, LOW);
  digitalWrite(SimConnectionPin, LOW);
  
  /*
   * Iniciar comunicaciones seriales. Solo podemos escuchar un ss a la vez cambiando así
   * entre sim y gps según sea necesario
   */
  Serial.begin(SerialBaudrate);
  sim_ss.begin(SimBaudrate);
  gps_ss.begin(GPSBaud);
  sim_ss.listen();

  Serial.println("Esperando a init");
  // Espere unos segundos para que el módulo pueda tomar comandos AT
  delay(5000);
  Serial.println("Inicial ... esperando hasta que el módulo se conecte a la red.");

  // Comience con la comunicación. Esto configura la transmisión automática y permite que el módulo envíe datos.
  sim_ss.println("AT");
  // Espere hasta que el módulo esté conectado y listo
  waitUntilResponse("SMS Ready");
  blinkLed(SimConnectionPin);
  

  // Modo completo
  sim_ss.println("AT+CFUN=1");
  waitUntilResponse("OK");
  blinkLed(SimConnectionPin);
  
  // Establecer credenciales (el nombre de usuario y la contraseña de TODO no se pueden configurar desde las variables). Esto puede funcionar sin CSTT y CIICR, pero a veces causó un error sin ellos aunque el APBR es dado por SAPBR
  sim_ss.write("AT+CSTT=\"");
  sim_ss.print(apn);
  sim_ss.write("\",\"\",\"\"\r\n");
  waitUntilResponse("OK");
  blinkLed(SimConnectionPin);
  
  // Conectar y obtener IP
  sim_ss.println("AT+CIICR");
  waitUntilResponse("OK");
  blinkLed(SimConnectionPin);
  
  // Algunas credenciales más
  sim_ss.write("AT+SAPBR=3,1,\"APN\",\"");
  sim_ss.print(apn);
  sim_ss.write("\"\r\n");
  waitUntilResponse("OK");
  blinkLed(SimConnectionPin);
  
  sim_ss.println("AT+SAPBR=3,1,\"USER\",\"\"");
  waitUntilResponse("OK");
  blinkLed(SimConnectionPin);
  
  sim_ss.println("AT+SAPBR=3,1,\"PWD\",\"\"");
  waitUntilResponse("OK");
  blinkLed(SimConnectionPin);
  
  sim_ss.println("AT+SAPBR=1,1");
  waitUntilResponse("OK");
  blinkLed(SimConnectionPin);
  
  sim_ss.println("AT+HTTPINIT");
  waitUntilResponse("OK");
  digitalWrite(SimConnectionPin, HIGH);

  gps_ss.listen();
  previous = millis();
  Serial.println("starting loop!");
}

void blinkLed(int led)
{
  digitalWrite(led, HIGH);
  delay(20);
  digitalWrite(led, LOW);
}

/*
 *  Lee de la serie SIM hasta que tengamos respuesta conocida. TODO error de manejo!
 * */
void waitUntilResponse(String response)
{
  beltTime = millis();
  responseString="";
  String totalResponse = "";
  while(responseString.indexOf(response) < 0 && millis() - beltTime < maxResponseTime)
  {
    readResponse();
    totalResponse = totalResponse + responseString;
    Serial.println(responseString);
  }

  if(totalResponse.length() <= 0)
  {
    Serial.println("No hay respuesta del módulo. Compruebe el cableado, la tarjeta SIM y la alimentación!");
    digitalWrite(ErrorPin, HIGH);
    delay(30000);
    exit(0); // No hay manera de recuperarse
  }
  else if (responseString.indexOf(response) < 0)
  {
    Serial.println("Respuesta inesperada del módulo.");
    Serial.println(totalResponse);
    digitalWrite(ErrorPin, HIGH);
    delay(30000);
    exit(0); // No hay manera de recuperarse
  }
}

/*
 * Lea desde la serie hasta que obtengamos la línea de respuesta que termina con el separador de línea
 * */
void readResponse()
{
  responseString = "";
  while(responseString.length() <= 0 || !responseString.endsWith("\n"))
  {
    tryToRead();

    if(millis() - beltTime > maxResponseTime)
    {
      return;
    }
  }
}

/*
 * Si tenemos algo disponible en la serie, agréguelo a la cadena de respuesta
 * */
void tryToRead()
{
  while(sim_ss.available())
  {
    char c = sim_ss.read();  //obtiene un byte del búfer serial
    responseString += c; //hace la cadena readString
  }
}

void loop()
{
  // Si tenemos datos, decodificar y registrar los datos.
  while (gps_ss.available() > 0)
   if (gps.encode(gps_ss.read()))
    logInfo();

  // Prueba que hemos tenido algo del módulo GPS en los primeros 10 segundos
  if (millis() - previous > 10000 && gps.charsProcessed() < 10)
  {
    Serial.println("GPS wiring error!");
    while(true);
  }
}

void logInfo()
{
  // Nos hace esperar hasta que tengamos una solución satelital
  if(!gps.location.isValid())
  {
    Serial.println("No es una ubicación válida. Esperando datos satelitales.");
    blinkLed(ErrorPin);
    return;
  }

  // Solo se registra una vez por frecuencia
  if(millis() - previous > frequency)
  {
    sim_ss.listen();
    previous = millis();
    String url = "AT+HTTPPARA=\"URL\",\"http://";
    url += serverIP;
    url += "/map/log.php?key=";
    url += loggingPassword;
    url += "&coordinates=";
    url += String(gps.location.lat(), DEC);
    url += ",";
    url += String(gps.location.lng(), DEC);
    url += "\"";
    sim_ss.println(url);
    waitUntilResponse("OK");
    digitalWrite(SimConnectionPin, LOW);
    sim_ss.println("AT+HTTPACTION=0");
    waitUntilResponse("+HTTPACTION:");
    digitalWrite(SimConnectionPin, HIGH);
    gps_ss.listen();
  }
}
