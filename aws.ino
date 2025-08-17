
#define MODEM Serial
const int MODEM_BAUD = 115200;
unsigned long lastSendTime = 0;
int counter = 0;

const char APN[]  = "airtelgprs.com";
const char HOST[] = "e8l1s9g0bk.execute-api.us-east-2.amazonaws.com";
const char PATH[] = "/test/DynamoDBManager";
const int  PORT   = 443;

const unsigned long SEND_INTERVAL_MS = 30000UL;


bool checkTLSCapability() {
  Serial.println("Checking TLS capability...");
  sendAT("AT+CSSLCFG?");
  String resp = getResponse(3000);
  Serial.println("CSSLCFG response: " + resp);
  if (resp.indexOf("ERROR") != -1) {
    Serial.println("Modem does not support CSSLCFG (TLS config).");
    return false;
  }
  return true;
}

bool httpPostAWS(const String &json) {
  Serial.println("Setting up HTTPS connection...");
  sendAT("AT+HTTPTERM");
  waitFor("OK", 2000);

  sendAT("AT+HTTPINIT");
  if (!waitFor("OK", 5000)) {
    Serial.println("HTTP init failed");
    return false;
  }

  bool tlsOk = false;
  sendAT("AT+CSSLCFG=\"SSLVERSION\",1,3");
  if (waitFor("OK", 3000)) {
    tlsOk = true;
    Serial.println("TLS version set to 1.2");
  } else {
    Serial.println("Failed to set TLS version, continuing...");
  }

  sendAT("AT+HTTPSSL=1");
  if (!waitFor("OK", 5000)) {
    Serial.println("HTTPSSL=1 failed. Your modem may NOT support HTTPS to AWS. Try firmware update, or check SIMCOM docs.");
    sendAT("AT+HTTPTERM");
    return false;
  }

  sendAT("AT+HTTPPARA=\"CID\",1");
  if (!waitFor("OK", 3000)) {
    Serial.println("HTTPPARA CID failed. PDP context may not be active.");
    sendAT("AT+HTTPTERM");
    return false;
  }

  String urlCmd = "AT+HTTPPARA=\"URL\",\"https://" + String(HOST) + String(PATH) + "\"";
  sendAT(urlCmd);
  if (!waitFor("OK", 5000)) {
    Serial.println("URL setup failed");
    sendAT("AT+HTTPTERM");
    return false;
  }

  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  waitFor("OK", 3000);

  String dataCmd = "AT+HTTPDATA=" + String(json.length()) + ",10000";
  sendAT(dataCmd);
  if (!waitFor("DOWNLOAD", 10000)) {
    Serial.println("HTTP data setup failed");
    sendAT("AT+HTTPTERM");
    return false;
  }

  MODEM.print(json);
  if (!waitFor("OK", 10000)) {
    Serial.println("Data upload failed");
    sendAT("AT+HTTPTERM");
    return false;
  }

  Serial.println("Executing POST request...");
  sendAT("AT+HTTPACTION=1");
  bool responseReceived = false;
  unsigned long start = millis();
  String httpactionLine = "";
  while (millis() - start < 30000) {
    String line = readModemLine(1000);
    if (line.indexOf("+HTTPACTION:") != -1) {
      responseReceived = true;
      httpactionLine = line;
      Serial.println("HTTP response received: " + line);
      break;
    }
    yield();
  }
  if (!responseReceived) {
    Serial.println("HTTP response timeout");
    sendAT("AT+HTTPTERM");
    return false;
  }

  sendAT("AT+HTTPREAD");
  String response = getResponse(10000);
  Serial.println("HTTP body:");
  Serial.println(response);

  sendAT("AT+HTTPTERM");
  waitFor("OK", 3000);

  if (httpactionLine.length() > 0) {
    int comma1 = httpactionLine.indexOf(',');
    int comma2 = httpactionLine.indexOf(',', comma1 + 1);
    if (comma1 != -1 && comma2 != -1) {
      String httpCode = httpactionLine.substring(comma1 + 1, comma2);
      if (httpCode != "200") {
        Serial.println("Warning: HTTP response code is not 200! (" + httpCode + ")");
      }
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  MODEM.begin(MODEM_BAUD);
  delay(5000);
  Serial.println("=== A7672S 4G LTE AWS HTTPS POST Test ===");
  if (!initializeModem()) {
    Serial.println("Modem initialization failed!");
    while (1) {
      Serial.println("Retrying in 15 seconds...");
      delay(15000);
      if (initializeModem()) break;
    }
  }
  // PATCH: Check TLS capability at startup
  checkTLSCapability();
  Serial.println("Modem ready!");
  lastSendTime = millis();
}

void loop() {
  if (millis() - lastSendTime > SEND_INTERVAL_MS) {
    counter++;
    String json = "{\"operation\":\"create\",\"payload\":{\"Item\":{\"id\":\"" + String(counter) + "\",\"temperature\":25.5,\"timestamp\":\"2025-08-14T16:33:21Z\"}}}";
    Serial.println("\n=== Sending JSON: " + json + " ===");
    if (!setupLTE()) {
      Serial.println("LTE setup failed, skipping this attempt");
    } else if (httpPostAWS(json)) {
      Serial.println("✓ HTTPS POST to AWS successful!");
    } else {
      Serial.println("✗ HTTPS POST to AWS failed!");
    }
    lastSendTime = millis();
  }
  yield();
}
