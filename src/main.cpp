#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <SPIFFS.h>

const char *key_map[4][4] = {
    {"reset", "0", "chime", "omit"},
    {"prog", "9", "6", "3"},
    {"part", "8", "5", "2"},
    {"full", "7", "4", "1"}};

const int keypress_delay = 200;

int setup_button = 4;
int bell_sense = 25;
int sw_sense = 33;
int strobe_sense = 32;

int collectors[] = {13, 12, 14, 27};
int emitters[] = {26, 2, 18, 23};

LiquidCrystal_I2C lcd(0x27, 16, 2);

IPAddress local_ip(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

Preferences preferences;

AsyncWebServer server(80);

boolean setup_mode_enabled()
{
   return digitalRead(setup_button) == LOW;
}

char *get_code(const char *code_ref)
{
   if (!preferences.isKey(code_ref))
   {
      return NULL;
   }
   String code = preferences.getString(code_ref);
   if (code == "")
   {
      return NULL;
   }
   else
   {
      char *codeChar = (char *)malloc((code.length() + 1) * sizeof(char));
      if (codeChar != NULL)
      {
         strcpy(codeChar, code.c_str());
      }
      return codeChar;
   }
}

void press_key(const char *key)
{
   Serial.print("Pressing key ");
   Serial.println(key);

   for (int collector = 0; collector < 4; collector++)
   {
      for (int emitter = 0; emitter < 4; emitter++)
      {
         if (strcmp(key_map[collector][emitter], key) == 0)
         {
            digitalWrite(collectors[collector], LOW);
            digitalWrite(emitters[emitter], LOW);
            vTaskDelay(keypress_delay / portTICK_PERIOD_MS);
            digitalWrite(collectors[collector], HIGH);
            digitalWrite(emitters[emitter], HIGH);
            return;
         }
      }
   }
}

void handle_keypress_route(AsyncWebServerRequest *request)
{
   String sequence = request->getParam("sequence")->value();

   xTaskCreate([](void *param)
               {
                String seq = *static_cast<String *>(param);
                delete static_cast<String *>(param);

                char seq_cstr[seq.length() + 1];
                seq.toCharArray(seq_cstr, seq.length() + 1);

                char *token = strtok(seq_cstr, ",");
                while (token != NULL)
                {
                  if (strcmp(token, "eng") == 0)
                  {
                     char *code = get_code("code_eng");
                     if (code != NULL)
                     {
                        char container[2] = {'\0', '\0'};
                        int index = 0;
                        while (code[index] != '\0')
                        {
                           container[0] = code[index];
                           press_key(container);
                           vTaskDelay(keypress_delay / portTICK_PERIOD_MS);
                           index++;
                        }
                        free(code);
                     }
                     else
                     {
                        Serial.println("Engineering code has not been set!");
                     }
                  }
                  else if (strcmp(token, "user") == 0)
                  {
                     char *code = get_code("code_user");
                     if (code != NULL)
                     {
                        char container[2] = {'\0', '\0'};
                        int index = 0;
                        while (code[index] != '\0')
                        {
                           container[0] = code[index];
                           press_key(container);
                           vTaskDelay(keypress_delay / portTICK_PERIOD_MS);
                           index++;
                        }
                        free(code);
                     }
                     else
                     {
                        Serial.println("User code has not been set!");
                     }
                  }
                  else
                  {
                     press_key(token);
                     vTaskDelay(keypress_delay / portTICK_PERIOD_MS);
                  }
                  token = strtok(NULL, ",");
                }

                vTaskDelete(NULL); },
               "KeyPressTask", 4096, new String(sequence), 1, NULL);

   request->send(200, "text/html", "OK");
}

void init_pins()
{
   pinMode(setup_button, INPUT_PULLUP);
   pinMode(bell_sense, INPUT_PULLUP);
   pinMode(sw_sense, INPUT_PULLUP);
   pinMode(strobe_sense, INPUT_PULLUP);

   for (int i = 0; i < 4; i++)
   {
      pinMode(collectors[i], OUTPUT);
      digitalWrite(collectors[i], HIGH);
      pinMode(emitters[i], OUTPUT);
      digitalWrite(emitters[i], HIGH);
   }

   Serial.println("Initialised IO pins");
}

void print_lcd(String line1, String line2)
{
   lcd.clear();
   lcd.setCursor(0, 0);
   lcd.print(line1);
   lcd.setCursor(0, 1);
   lcd.print(line2);
}

void handle_setup_route(AsyncWebServerRequest *request)
{
   request->send(SPIFFS, "/setup.html", "text/html");
}

void handle_status_route(AsyncWebServerRequest *request)
{
   int sw_state = digitalRead(sw_sense);
   int bell_state = digitalRead(bell_sense);
   int strobe_state = digitalRead(strobe_sense);
   String output = "";
   output.concat("SW:");
   output.concat(sw_state);
   output.concat("\n");
   output.concat("Bell:");
   output.concat(bell_state);
   output.concat("\n");
   output.concat("Strobe:");
   output.concat(strobe_state);
   request->send(200, "text/html", output);
}

void handle_code_update_route(AsyncWebServerRequest *request)
{
   if (!setup_mode_enabled())
   {
      request->send(412, "text/html", "Setup switch must be in the GREEN position");
      return;
   }

   if (!request->hasParam("type") || !request->hasParam("code"))
   {
      request->send(400, "text/html", "You must specify both a code and a type! (Valid types: engineer, user)");
      return;
   }

   String type = request->getParam("type")->value();
   String code = request->getParam("code")->value();
   if (type == "" || code == "")
   {
      request->send(400, "text/html", "Code and type cannot be empty");
      return;
   }

   if (type != "engineer" && type != "user")
   {
      request->send(400, "text/html", "Type must be engineer or user");
      return;
   }

   if (type == "engineer")
   {
      preferences.putString("code_eng", code);
   }
   else if (type == "user")
   {
      preferences.putString("code_user", code);
   }

   String message = "Changed ";
   message.concat(type);
   message.concat(" code");
   request->send(200, "text/html", message);
   return;
}

void handle_wifi_details_change_route(AsyncWebServerRequest *request)
{
   if (!setup_mode_enabled())
   {
      request->send(412, "text/html", "Setup switch must be in the GREEN position");
      return;
   }

   if (!request->hasParam("ssid") || !request->hasParam("password"))
   {
      request->send(400, "text/html", "You must specify both an SSID and password");
      return;
   }

   String ssid = request->getParam("ssid")->value();
   String password = request->getParam("password")->value();
   if (ssid == "" || password == "")
   {
      request->send(400, "text/html", "SSID and password cannot be empty");
      return;
   }

   preferences.putString("ssid", ssid);
   preferences.putString("password", password);
   request->send(200, "text/html", "Rebooting in 10 seconds to implement changes");
   print_lcd("Setup Finished", "Reboot in 10s");
   delay(10000);
   esp_restart();
}

void setup_mode()
{
   Serial.println("Welcome to setup mode!");
   print_lcd("Setup Mode", "192.168.1.1");
   WiFi.softAP("Veritasium", "123456789");
   WiFi.softAPConfig(local_ip, gateway, subnet);

   server.on("/", HTTP_GET, handle_setup_route);
   server.on("/update-wifi", HTTP_POST, handle_wifi_details_change_route);
   server.on("/update-code", HTTP_POST, handle_code_update_route);
   server.begin();
}

char *string_to_char(String input)
{
   char *output = new char[input.length() + 1];
   memset(output, 0, input.length() + 1);

   for (int i = 0; i < input.length(); i++)
      output[i] = input.charAt(i);
   return output;
}

void business_as_usual(String ssid, String password)
{
   Serial.println("Connecting to WiFi");
   Serial.print("SSID: ");
   Serial.println(ssid);
   Serial.print("Password: ");
   Serial.println(password);

   print_lcd("Connecting WiFi", ssid);
   WiFi.setHostname("Veritasium");
   WiFi.begin(string_to_char(ssid), string_to_char(password));

   while (WiFi.status() != WL_CONNECTED)
   {
      delay(1000);
      Serial.println("Waiting for WiFi...");
      Serial.print("Current status: ");
      Serial.println(WiFi.status());
   }

   Serial.println("WiFi connected!");
   Serial.print("IP address: ");
   Serial.println(WiFi.localIP());
   print_lcd("Veritasium", WiFi.localIP().toString());

   server.on("/", HTTP_GET, handle_setup_route);
   server.on("/update-wifi", HTTP_POST, handle_wifi_details_change_route);
   server.on("/status", HTTP_GET, handle_status_route);
   server.on("/press", HTTP_POST, handle_keypress_route);
   server.on("/update-code", HTTP_POST, handle_code_update_route);

   server.begin();
   delay(3000);
}

void setup()
{
   lcd.init();
   lcd.backlight();

   preferences.begin("veritasium", false);
   Serial.begin(115200);
   Serial.println("Veritasium - Booting up...");

   if (!SPIFFS.begin(true))
   {
      Serial.println("An error has occurred while mounting SPIFFS");
      return;
   }
   init_pins();

   if (setup_mode_enabled())
   {
      Serial.println("Setup button pressed");
      setup_mode();
   }
   else
   {
      String ssid = preferences.getString("ssid");
      String password = preferences.getString("password");

      if (ssid == "" || password == "")
      {
         Serial.println("No credentials saved. Going into setup mode");
         setup_mode();
      }
      else
      {
         business_as_usual(ssid, password);
      }
   }
}

void loop()
{
   // Nada
}