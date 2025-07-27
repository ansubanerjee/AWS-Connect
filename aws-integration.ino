#include <SoftwareSerial.h>

SoftwareSerial sim800(8, 9); // RX, TX
unsigned long lastSendTime = 0;
int counter = 0;

void flushSIM800() {
  while (sim800.available()) sim800.read();
}

void sendAT(String cmd) {
  sim800.println(cmd);
  Serial.println(">> " + cmd);
}

bool waitFor(String expected, int timeout = 5000) {
  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (sim800.available()) {
      char c = sim800.read();
      response += c;
      if (response.indexOf(expected) != -1) {
        Serial.println("<< " + response);
        return true;
      }
    }
  }
  Serial.println("<< " + response);
  Serial.println("ERROR: '" + expected + "' not found\n");
  return false;
}

bool waitForNetworkRegistration(int retries = 10) {
  for (int i = 0; i < retries; i++) {
    sendAT("AT+CREG?");
    if (waitFor("0,1", 3000) || waitFor("0,5", 3000)) return true;
    delay(3000);
  }
  return false;
}

bool initializeSIM800() {
  flushSIM800();

  sendAT("AT");
  if (!waitFor("OK", 3000)) {
    Serial.println("No response at 9600, trying 115200...");
    sim800.end();
    delay(1000);
    sim800.begin(115200);
    flushSIM800();
    delay(2000);
    sendAT("AT");
    if (!waitFor("OK", 3000)) {
      return false;
    }
  }

  sendAT("ATE0");
  waitFor("OK");

  sendAT("AT+CPIN?");
  if (!waitFor("READY")) return false;

  sendAT("AT+CSQ");
  waitFor("OK");

  if (!waitForNetworkRegistration()) return false;

  sendAT("AT+CGATT?");
  if (!waitFor("1")) return false;

  sendAT("AT+CIPSHUT");
  waitFor("SHUT OK", 5000);

  sendAT("AT+CIPMUX=0");
  waitFor("OK");

  sendAT("AT+CIPQSEND=0");
  waitFor("OK");

  sendAT("AT+CIPRXGET=1");
  waitFor("OK");

  sendAT("AT+CSTT=\"internet\"");
  if (!waitFor("OK")) return false;

  sendAT("AT+CIICR");
  if (!waitFor("OK", 10000)) return false;

  sendAT("AT+CIFSR");
  waitFor(".", 5000);  

  return true;
}

bool connectTCP(String host, int port) {
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print("Connecting to ");
    Serial.print(host);
    Serial.print(":");
    Serial.print(port);
    Serial.print(" (Attempt ");
    Serial.print(attempt);
    Serial.println(")");

    String cmd = "AT+CIPSTART=\"TCP\",\"" + host + "\"," + String(port);
    sendAT(cmd);
    if (waitFor("CONNECT OK", 60000)) {
      return true;
    }

    Serial.println("No CONNECT OK, retrying...");
    sendAT("AT+CIPCLOSE");
    waitFor("CLOSE OK", 5000);
    delay(2000);
  }
  return false;
}

void readResponseWithCIPRXGET() {
  bool dataAvailable = true;
  while (dataAvailable) {
    delay(2000); 
    sendAT("AT+CIPRXGET=2,1460");
    String response = "";
    unsigned long start = millis();
    while (millis() - start < 5000) {
      while (sim800.available()) {
        char c = sim800.read();
        response += c;
      }
    }
    if (response.indexOf("+CIPRXGET: 2,0") != -1) {
      dataAvailable = false; 
    }
    Serial.println(response);
  }
}

bool sendViaTCP(String host, String path, String json) {
  sendAT("AT+CIPSHUT");
  waitFor("SHUT OK", 5000);

  sendAT("AT+CSTT=\"internet\"");
  waitFor("OK", 5000);

  sendAT("AT+CIICR");
  waitFor("OK", 10000);

  sendAT("AT+CIFSR");
  waitFor(".", 5000);

  if (!connectTCP(host, 80)) {
    Serial.println("TCP connection failed");
    return false;
  }

  String request = "POST " + path + " HTTP/1.1\r\n";
  request += "Host: " + host + "\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Content-Length: " + String(json.length()) + "\r\n";
  request += "Connection: close\r\n\r\n";
  request += json;

  Serial.println("---- FULL HTTP REQUEST ----");
  Serial.println(request);
  Serial.println("---- END ----");

  sendAT("AT+CIPSEND");
  if (!waitFor(">", 10000)) {
    sendAT("AT+CIPCLOSE");
    waitFor("CLOSE OK", 5000);
    sendAT("AT+CIPSHUT");
    waitFor("SHUT OK", 5000);
    return false;
  }

  Serial.println(">> Sending data...");
  sim800.print(request);
  sim800.write(26); 

  if (!waitFor("SEND OK", 20000)) {
    Serial.println("SEND OK not received");
    sendAT("AT+CIPCLOSE");
    waitFor("CLOSE OK", 5000);
    sendAT("AT+CIPSHUT");
    waitFor("SHUT OK", 5000);
    return false;
  }

  Serial.println("Waiting for server response...");
  readResponseWithCIPRXGET();
  Serial.println("---- END OF RESPONSE ----");

  sendAT("AT+CIPCLOSE");
  waitFor("CLOSE OK", 5000);

  return true;
}

void setup() {
  Serial.begin(9600);
  sim800.begin(9600);

  delay(5000);

  Serial.println("Initializing SIM800...");
  if (!initializeSIM800()) {
    Serial.println("SIM800 Initialization Failed!");
    while (1);
  }
  Serial.println("SIM800 Ready!");
}

void loop() {
  if (millis() - lastSendTime > 30000) {
    counter++;

    float voltage = random(350, 401) / 100.0;
    float current = random(100, 201) / 100.0;
    int altitude = random(120, 131);
    float mag = random(20, 51) / 100.0;
    String timestamp = String("2025-07-13T22:") +
                       (counter < 10 ? "0" : "") +
                       String(counter) + ":00";

    String id = "111111_" + String(counter);

    String json = String("{\"operation\":\"create\",\"payload\":{\"Item\":{") +
                  "\"id\":\"" + id + "\"," +
                  "\"voltage\":" + String(voltage, 2) + "," +
                  "\"current\":" + String(current, 2) + "," +
                  "\"altitude\":" + String(altitude) + "," +
                  "\"mag\":" + String(mag, 2) + "," +
                  "\"timestamp\":\"" + timestamp + "\"}}}";

    Serial.println("Sending JSON: " + json);

    String host = "34.199.40.64";  
    String path = "/post";

    if (!sendViaTCP(host, path, json)) {
      Serial.println("Failed to send data via TCP.");
    }

    lastSendTime = millis();
  }
}
