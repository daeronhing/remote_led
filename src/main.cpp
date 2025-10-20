#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>

#define MAIN_LIGHT_PIN 2
#define LED_PIN 4
#define LED_COUNT 300

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

const char *ssid = "587home";
const char *password = "4gsemiconz";
const char *hostname = "esp32-room-light";
int channel;
uint8_t bssid[6];

const char *mqtt_broker = "192.168.50.84";
const char *mqtt_username = "daeronhing";
const char *mqtt_password = "d@er0n_h1ng";
const char *command_topic = "/my_room/light/set";
const char *main_light_command_topic = "/my_room/main_light/set";
const char *state_topic = "/my_room/light/state";
const int max_mqtt_reconnect_attempts = 5;

const uint8_t max_brightness = 255;
uint8_t red = 255;
uint8_t green = 255;
uint8_t blue = 255;
uint8_t brightness = 128;
bool led_switch_on = false;
bool need_update = false;

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

bool connect_mqtt();
void get_board_info();
void mqtt_callback(char *topic, byte *payload, unsigned int length);
void set_led_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
void switch_main_light_on(bool on);

void setup()
{
	strip.begin();
	strip.setBrightness(max_brightness);
	// set_led_color(red, green, blue, brightness);
	strip.clear();
	strip.show();

	pinMode(MAIN_LIGHT_PIN, OUTPUT);
	digitalWrite(MAIN_LIGHT_PIN, LOW);

	Serial.begin(115200);
	get_board_info();

	WiFi.persistent(false);
	WiFi.mode(WIFI_STA);
	WiFi.hostname(hostname);
	WiFi.begin(ssid, password);

	Serial.println("Connecting to WiFi");
	while (WiFi.waitForConnectResult() != WL_CONNECTED)
	{
		Serial.println("Connection Failed! Rebooting...");
		delay(3000);
		ESP.restart();
	}
	Serial.println("WiFi Connected");
	Serial.print("MAC Address: ");
	Serial.println(WiFi.macAddress());
	
	channel = WiFi.channel();
	memcpy(bssid, WiFi.BSSID(), 6);

	ArduinoOTA.setPort(3232);
	ArduinoOTA.setHostname("esp32-room-light-ota");
	ArduinoOTA.setPassword("d@er0n_h1ng");
	ArduinoOTA
		.onStart([]()
				 {
					String type;
					if (ArduinoOTA.getCommand() == U_FLASH) {
						type = "sketch";
					} else {  // U_SPIFFS
						type = "filesystem";
					}

      				// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
					Serial.println("Start updating " + type); })
		.onEnd([]()
			   { Serial.println("\nEnd"); })
		.onProgress([](unsigned int progress, unsigned int total)
					{ Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
		.onError([](ota_error_t error)
				 {
					Serial.printf("Error[%u]: ", error);
					if (error == OTA_AUTH_ERROR) {
						Serial.println("Auth Failed");
					} else if (error == OTA_BEGIN_ERROR) {
						Serial.println("Begin Failed");
					} else if (error == OTA_CONNECT_ERROR) {
						Serial.println("Connect Failed");
					} else if (error == OTA_RECEIVE_ERROR) {
						Serial.println("Receive Failed");
					} else if (error == OTA_END_ERROR) {
						Serial.println("End Failed");
					} });
	ArduinoOTA.begin();

	mqtt_client.setServer(mqtt_broker, 1883);
	mqtt_client.setCallback(mqtt_callback);
	String client_id = "espRoomLight" + String(random(0xffff), HEX);

	Serial.print("Connecting to MQTT Broker with client ID: ");
	Serial.println(client_id);
	for (int i = 0; i < max_mqtt_reconnect_attempts; i++)
	{
		Serial.print("MQTT connection attempt: ");
		Serial.println(i + 1);

		if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password))
		{
			Serial.println("Connected");
			mqtt_client.subscribe(command_topic);
			mqtt_client.subscribe(main_light_command_topic);
			break;
		}
		else if (i < (max_mqtt_reconnect_attempts - 1))
		{
			Serial.println("MQTT connection failed. Reconnecting...");
			delay(2000);
		}
		else
		{
			Serial.println("MQTT connection failed. Rebooting...");
			ESP.restart();
		}
	}
}

bool reconnecting = false;
void loop()
{
	if (WiFi.isConnected())
	{
		ArduinoOTA.handle();
		if (reconnecting)
		{
			reconnecting = false;
			Serial.println("Reconnected to WiFi");
		}

		if (!mqtt_client.connected())
		{
			Serial.println("Reconnecting to MQTT Broker");
			connect_mqtt();
		}
		else
		{
			mqtt_client.loop();
		}
	}

	else if (!reconnecting)
	{
		Serial.println("Reconnecting WiFi");
		WiFi.begin(ssid, password, channel, bssid);
		reconnecting = true;
	}

	if (need_update)
	{
		if (led_switch_on)
		{
			set_led_color(red, green, blue, brightness);
		}
		else
		{
			strip.clear();
			strip.show();
		}
		need_update = false;
	}
}

bool connect_mqtt()
{
	String client_id = "espRoomLight" + String(random(0xffff), HEX);
	Serial.print("Connecting to MQTT Broker with client ID: ");
	Serial.println(client_id);
	if (mqtt_client.connect(client_id.c_str(), mqtt_username, mqtt_password))
	{
		Serial.println("Connected");
		mqtt_client.subscribe(command_topic);
		mqtt_client.subscribe(main_light_command_topic);
		return true;
	}
	else
	{
		Serial.println("MQTT connection failed");
		return false;
	}
}

void get_board_info()
{
	Serial.println("Hello world!\n");

	/* Print chip information */
	esp_chip_info_t chip_info;
	uint32_t flash_size;
	esp_chip_info(&chip_info);
	Serial.print("This is ");
	Serial.print(CONFIG_IDF_TARGET);
	Serial.print(" chip with ");
	Serial.print(chip_info.cores);
	Serial.print(" CPU core(s), WiFi");
	Serial.print((chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "");
	Serial.println((chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

	unsigned major_rev = chip_info.revision / 100;
	unsigned minor_rev = chip_info.revision % 100;
	Serial.print("Chip revision: ");
	Serial.print(major_rev);
	Serial.print('.');
	Serial.println(minor_rev);

	if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
	{
		Serial.println("Failed to get flash size, probe failed\n");
		return;
	}

	Serial.print("Flash size: ");
	Serial.print(flash_size / (1024 * 1024));
	Serial.print("MB ");
	Serial.println((chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

	Serial.print("Minimum free heap size: ");
	Serial.print(esp_get_minimum_free_heap_size());
	Serial.println(" bytes\n");
}

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
	JsonDocument command_doc;
	DeserializationError error = deserializeJson(command_doc, payload);

	// Check which topic did the message come from
	if (error.code() != DeserializationError::Ok)
	{
		Serial.print("Error: ");
		Serial.println(error.c_str());
		return;
	}

	else if (strcmp(topic, main_light_command_topic) == 0)
	{
		if (command_doc["state"] == "ON")
		{
			switch_main_light_on(true);
		}
		else if (command_doc["state"] == "OFF")
		{
			switch_main_light_on(false);
		}
	}

	else if (strcmp(topic, command_topic) == 0)
	{
		// if (command_doc.containsKey("state"))
		if (!command_doc["state"].isNull())
		{
			const char *state = command_doc["state"];
			Serial.print("State: ");
			Serial.println(state);
			if (strcmp(state, "ON") == 0)
			{
				led_switch_on = true;
			}
			else if (strcmp(state, "OFF") == 0)
			{
				led_switch_on = false;
			}
		}

		// if (command_doc.containsKey("brightness"))
		if (!command_doc["brightness"].isNull())
		{
			int _brightness = command_doc["brightness"];
			Serial.print("Brightness: ");
			Serial.println(_brightness);
			brightness = _brightness;
		}

		// if (command_doc.containsKey("color"))
		if (!command_doc["color"].isNull())
		{
			int _r = command_doc["color"]["r"];
			int _g = command_doc["color"]["g"];
			int _b = command_doc["color"]["b"];
			Serial.print("Color: ");
			Serial.print(_r);
			Serial.print(", ");
			Serial.print(_g);
			Serial.print(", ");
			Serial.println(_b);
			red = _r;
			green = _g;
			blue = _b;
		}
		need_update = true;
	}
}

void set_led_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
	uint8_t _red = map(red, 0, 255, 0, brightness);
	uint8_t _green = map(green, 0, 255, 0, brightness);
	uint8_t _blue = map(blue, 0, 255, 0, brightness);

	for (int i = 0; i < LED_COUNT; i++)
	{
		strip.setPixelColor(i, _red, _green, _blue);
	}

	strip.show();
}

void switch_main_light_on(bool on)
{
	if (on)
	{
		digitalWrite(MAIN_LIGHT_PIN, LOW);
	}
	else
	{
		digitalWrite(MAIN_LIGHT_PIN, HIGH);
	}
}