#pragma once

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <Ticker.h>
#include "ntp_client.h"


#include "sensor_data.h"
#include "display_data.h"


class weather_station_base
{
public:
	// Initialize DHT sensor 
	// NOTE: For working with a faster than ATmega328p 16 MHz Arduino chip, like an ESP8266,
	// you need to increase the threshold for cycle counts considered a 1 or 0.
	// You can do this by passing a 3rd parameter for this threshold.  It's a bit
	// of fiddling to find the right value, but in general the faster the CPU the
	// higher the value.  The default for a 16mhz AVR is a value of 6.  For an
	// Arduino Due that runs at 84mhz a value of 30 works.
	// This is for the ESP8266 processor on ESP-01 
	weather_station_base(const char *host_ssid, const char *host_password, uint8_t dht_pin, uint8_t dht_type);

	void init();

	void handle_root();

	void handle_sensor_data_post();

	void on_loop();

protected:
	static void update_display_timer_func(weather_station_base *ws_base);
	void update_display(bool update_now = false);
	void update_local_sensor_data(bool update_now = false);
	void update_time(bool update_now = false);

	static weather_station_base *_this;
	static void sensor_data_post_handler();
	static void root_handler();

private:
	void draw_display();
	bool m_wifi_connected;
	time_t m_last_time_update;
	time_t m_last_local_sensor_update;
	String m_host_ssid;
	String m_host_password;
	DHT m_dht;
	ESP8266WebServer m_server;
	NtpClient m_ntp_client;
	Ticker m_display_timer;
	Ticker m_update_time_timer;
	sensor_data m_current_local_sensor_data;
	sensor_data m_current_remote_sensor_data;
	display_data m_last_display_data;
};