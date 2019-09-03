/**
 * MIT License
 * Copyright (c) 2019 Tim Huetz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "weather_station/version.h"
#include "weather_station/wifi.h"
#include <Adafruit_BME280.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <RemoteDebug.h>
#include <pins_arduino.h>

void (*resetFunc)(void) = 0;
const uint16_t MAX_RAW_VOLTAGE = 818;
const uint16_t MIN_RAW_VOLTAGE = 601;

RemoteDebug Debug;

void indicateStillConnecting() {
	pinMode(LED_BUILTIN, OUTPUT);

	digitalWrite(LED_BUILTIN, LOW);
	delay(100);
	digitalWrite(LED_BUILTIN, HIGH);
	delay(100);
}

void indicateConnected() {
	pinMode(LED_BUILTIN, OUTPUT);

	digitalWrite(LED_BUILTIN, LOW);
	delay(2000);
	digitalWrite(LED_BUILTIN, HIGH);
}

unsigned long int measureRawBatteryVoltage() { return analogRead(PIN_A0); }

float calculateBatteryChargeInPercent(const float raw_voltage) {
	const float max_range = MAX_RAW_VOLTAGE - MIN_RAW_VOLTAGE;
	float percentage = ((raw_voltage - MIN_RAW_VOLTAGE) / max_range) * 100.0f;

	if (percentage > 100.0f) {
		percentage = 100.0f;
	}

	if (percentage < 0.0f) {
		percentage = 0.0f;
	}

	return percentage;
}

void sendMeasurements(float temp, float humidity, float pressure, float raw_voltage) {
	char tmp[9];
	sprintf(tmp, "%08X", ESP.getChipId());
	char version_str[9]; // would be able to store "10.10.10\0" so should be enough
	memset(version_str, 0, sizeof(char) * 9);
	sprintf(version_str, "%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
	float charge = calculateBatteryChargeInPercent(raw_voltage);
	HTTPClient http;
	BearSSL::WiFiClientSecure client;
	client.setInsecure();
	String postUrl = ENDPOINT_BASE;
	String postData = "{\"temperature\":";
	postData += String(temp);
	postData += ",\"humidity\":";
	postData += String(humidity);
	postData += ",\"pressure\":";
	postData += String(pressure);
	postData += ",\"raw_voltage\":";
	postData += String(raw_voltage);
	postData += ",\"charge\":";
	postData += String(charge);
	postData += ",\"sensor\":\"";
	postData += String(tmp);
	postData += "\",\"firmware_version\":\"";
	postData += String(version_str);
	postData += "\"}";
	debugD("Sending JSON: %s", postData.c_str());
	http.begin(client, postUrl);
	http.addHeader("Content-Type", "application/json");
	int httpCode = http.POST(postData);
	if (204 != httpCode) {
		debugE("%d - Could not send temperature to endpoint.", httpCode);
	}
	http.end();
	client.stop();
}

void measureAndShowValues() {
	Adafruit_BME280 bme;
	bool bme_status;
	bme_status = bme.begin(0x76); // address either 0x76 or 0x77
	if (!bme_status) {
		debugE("Could not find a valid BME280 sensor, check wiring!");
		return;
	}

	bme.setSampling(Adafruit_BME280::MODE_FORCED,
					Adafruit_BME280::SAMPLING_X1, // temperature
					Adafruit_BME280::SAMPLING_X1, // pressure
					Adafruit_BME280::SAMPLING_X1, // humidity
					Adafruit_BME280::FILTER_OFF);

	bme.takeForcedMeasurement();

	// Get temperature
	float measured_temp = bme.readTemperature();
	measured_temp = measured_temp + 0.0f;
	debugD("Temperature: %.2f °C", measured_temp);

	// Get humidity
	float measured_humi = bme.readHumidity();
	debugD("Humidity: %.2f %%", measured_humi);

	// Get pressure
	float measured_pres = bme.readPressure() / 100.0f;
	debugD("Pressure: %.2f hPa", measured_pres);

	// Show the current battery voltage
	float raw_voltage = measureRawBatteryVoltage();
	debugD("Raw battery voltage value: %.2f", raw_voltage);

	// Show the ChipID / Sensor ID
	debugD("ChipID: %08X;", ESP.getChipId());

	// ensure that we do not send inaccurate measurements which are caused by a too low voltage
	if (MIN_RAW_VOLTAGE >= raw_voltage) {
		debugW("Not sending last measurement since the raw_voltage (%.2f) droped to or below %.2f", raw_voltage, MIN_RAW_VOLTAGE);
		return;
	}

	// send it
	sendMeasurements(measured_temp, measured_humi, measured_pres, raw_voltage);
}

void setup() {
	int connectionTries = 0;

	// setup the serial interface with a specified baud rate
	Serial.begin(115200);

	// just print a simple header
	Serial.println();
	Serial.printf("Solar Powered Weather Station %d.%d.%d - Written by Tim Huetz. All rights reserved.\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
	Serial.printf("================================================================================");
	Serial.println();

	// try to connect to the wifi
	WiFi.hostname(WIFI_HOST);
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	while (WiFi.status() != WL_CONNECTED) {
		connectionTries++;
		indicateStillConnecting();
		delay(500);
		if (connectionTries > 20) {
			Serial.println();
			Serial.printf("Could not connect after %d tries, resetting and starting from the beginning...", connectionTries);
			resetFunc();
		}
	}
	indicateConnected();

	//
	Debug.begin(WIFI_HOST);
	Debug.showColors(true);
}

void loop() {
	measureAndShowValues();
	Debug.handle();
	delay(5000);
}
