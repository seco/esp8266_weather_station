#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <Ticker.h>
#include <Time.h>
#include <TimeLib.h>
#include <DHT.h>


#define DHTTYPE DHT22
#define DHTPIN  2
#define DISPLAY_UPDATE_INTERVAL_MS      1000              // 1 second
#define LOCAL_SENSOR_UPDATE_INTERVAL_MS (60 * 1000)       // 1 minute
#define UPDATE_TIME_UPDATE_INTERVAL_MS  (15 * 60 * 1000)  // 15 minutes
#define UDP_LISTEN_PORT                 8888
#define TEMP_REPORT_SERVER_LISTEN_PORT  8080


time_t getNtpTime(WiFiUDP & udp);

const char *ap_ssid         = "JRL_WS_0";
const char *ap_password     = "!v734@89h789g";
const char* ssid            = "Caradhras";
const char* password        = "Speak friend.";

// NTP Servers:
static const char ntpServerName[] = "us.pool.ntp.org";
//static const char ntpServerName[] = "time.nist.gov";
//static const char ntpServerName[] = "time-a.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-b.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-c.timefreq.bldrdoc.gov";

const int timeZone = 1;     // Central European Time
//const int timeZone = -5;  // Eastern Standard Time (USA)
//const int timeZone = -4;  // Eastern Daylight Time (USA)
//const int timeZone = -8;  // Pacific Standard Time (USA)
//const int timeZone = -7;  // Pacific Daylight Time (USA)

void reset()
{
  // TODO: send reset in GPIO 16
}

bool startAccessPoint()
{
  Serial.println("\r\nStarting WiFi Access Point");
  if (!WiFi.mode(WIFI_AP_STA))
  {
    Serial.println("Failed to set WiFi mode to access point + station");
    return false;
  }

  // Start the AP
  if (!WiFi.softAP(ap_ssid, ap_ssid))
  {
    Serial.println("Failed to start access point");
    return false;
  }
  Serial.print("Access point started with SSID ");
  Serial.println(ap_ssid);
  Serial.print("Access point IP address = ");
  Serial.println(WiFi.softAPIP());
  return true;
}

bool connectWiFi(int retries)
{
  // Connect to host WiFi network
  wl_status_t status = WiFi.begin(ssid, password);
  Serial.print("WiFi.begin status = ");
  Serial.println(status);
  Serial.print("\n\r \n\rWorking to connect");

  // Wait for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < retries) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("");
    Serial.print("Failed tp connect to ");
    Serial.println(ssid);
    return false;
  }
  
  Serial.println("");
  Serial.println("DHT Weather Reporting Client");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  return true;
}

struct sensor_data
{
  sensor_data() : temperature(NAN), humidity(NAN)
  {
    
  }
  float temperature;
  float humidity;
  bool operator==(const sensor_data & other) const
  {
    return temperature == other.temperature && humidity == other.humidity;
  }
  bool operator!=(const sensor_data & other) const
  {
    return !operator==(other);
  }
};

struct display_data
{
  sensor_data data;
  time_t time;
  bool changed(const sensor_data & new_sensor_data, time_t new_time) const
  {
    return !time_is_same(new_time) || new_sensor_data != data;
  }
  void update(const sensor_data & new_sensor_data, time_t new_time)
  {
    time = new_time;
    data = new_sensor_data;
  }
private:
  bool time_is_same(time_t new_time) const
  {
    // Ignore seconds;
    return minute(time) == minute(new_time) &&
           hour(time) == hour(new_time) &&
           day(time) == day(new_time) &&
           month(time) == month(new_time) &&
           year(time) == year(new_time);
  }
};

String pad_int(int val, int width, char fill)
{
  String s(val);
  if (s.length() < width)
  {
    String fill_str;
    for(int i = 0; i < width - s.length(); i++)
    {
      fill_str += fill;
    }

    return fill_str + s;
  }

  return s;
}

String time_string(const tmElements_t & tm)
{
  return pad_int(tm.Hour, 2, ' ') + ":" + pad_int(tm.Minute, 2, '0');
}

String date_string(const tmElements_t & tm)
{
  return pad_int(tm.Month, 2, ' ') + "/" + pad_int(tm.Day, 2, ' ') + "/" + tm.Year;
}

void sensor_data_post_handler();

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
  weather_station_base(uint8_t pin, uint8_t type) : m_wifi_connected(false), m_dht(pin, type, 11), m_server(TEMP_REPORT_SERVER_LISTEN_PORT) // 11 works fine for ESP8266
  {
    
  }
  // setup() methods
  void init()
  {
    m_dht.begin();
    if (!startAccessPoint())
    {
      Serial.println("Failed to start cccess point! Resetting processor...");
      reset();
    }

    m_wifi_connected = connectWiFi(60); // About 30 seconds
    if (!m_wifi_connected)
    {
      Serial.println("Failed to start WiFi! Continuing without WiFi");
    }

    Serial.println("Starting UDP");
    m_udp.begin(UDP_LISTEN_PORT);
    Serial.print("Local NTP UDP port: ");
    Serial.println(m_udp.localPort());

    Serial.println("Starting web server...\n");
    m_server.on("/post_sensor_data", HTTP_POST, sensor_data_post_handler);
    m_server.begin();
    Serial.println("Web server started.\n");

    // ???: Sleep for DHT? probably not needed, because there will be some delay connecting to the access point.

    // Get the initial data.
    update_time();
    update_local_sensor_data();
    // Draw the display
    draw_display();

    // Start the timers
    m_display_timer.attach_ms(DISPLAY_UPDATE_INTERVAL_MS, update_display_timer_func, this);
    m_update_sensor_timer.attach_ms(LOCAL_SENSOR_UPDATE_INTERVAL_MS, update_sensor_data_timer_func, this);
    m_update_time_timer.attach_ms(UPDATE_TIME_UPDATE_INTERVAL_MS, update_time_timer_func, this);
  }

  void handle_sensor_data_post()
  {
    String message = "Received sensor data post\n\n";
    message += "URI: ";
    message += m_server.uri();
    message += "\nArguments: ";
    message += m_server.args();
    message += "\n";
    for (uint8_t i = 0; i < m_server.args(); i++)
    {
      message += " " + m_server.argName(i) + ": " + m_server.arg(i) + "\n";
    }
    Serial.println(message);

    m_server.send(200, "text/plain", "OK");
  }
  
protected:
  static void update_display_timer_func(weather_station_base *ws_base)
  {
    ws_base->update_display();
  }
  void update_display()
  {
    // Get a snapshot of the time and sensor data.
    time_t current_time = now();
    sensor_data current_sensor_data = m_current_sensor_data;

    // See if it's different than what we last displayed.
    if (m_last_display_data.changed(current_sensor_data, current_time))
    {
      // Update the display if anything has changed.
      draw_display();

      m_last_display_data.update(current_sensor_data, current_time);  // Use same timestamp we checked against.
                                                                        // So that in case the time just advanced,
                                                                        // we'll know to update the display next time.
    }
  }

  static void update_sensor_data_timer_func(weather_station_base *ws_base)
  {
    ws_base->update_local_sensor_data();
  }
  void update_local_sensor_data()
  {
    m_current_sensor_data.humidity = m_dht.readHumidity();
    m_current_sensor_data.temperature = m_dht.readTemperature(true);
  }

  static void update_time_timer_func(weather_station_base *ws_base)
  {
    ws_base->update_time();
  }
  void update_time()
  {
    if (!m_wifi_connected)
    {
      m_wifi_connected = connectWiFi(60); // About 30 seconds
      if (!m_wifi_connected)
      {
        Serial.println("Failed to start WiFi! Skipping time update.");
        return;
      }
    }
    
    // Get the time via NTP
    time_t new_time = getNtpTime(m_udp);

    // Update the time
    if (new_time == 0)
    {
      return;
    }
    
    setTime(new_time);
  }
  
private:
  void draw_display()
  {
    tmElements_t tm;
    breakTime(now(), tm);

    // TODO: Go to LCD.
    String time_str = date_string(tm) + " " + time_string(tm) + " - " + (int)m_current_sensor_data.temperature + "F " + (int)m_current_sensor_data.humidity + "%";
    Serial.println(time_str);
  }
  bool m_wifi_connected;
  DHT m_dht;
  ESP8266WebServer m_server;
  WiFiUDP m_udp;
  Ticker m_display_timer;
  Ticker m_update_sensor_timer;
  Ticker m_update_time_timer;
  sensor_data m_current_sensor_data;
  display_data m_last_display_data;
} g_weather_station_base(DHTPIN, DHTTYPE);


void sensor_data_post_handler()
{
  g_weather_station_base.handle_sensor_data_post();
}

void setup()
{
  g_weather_station_base.init();
}

void loop()
{
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

void sendNTPpacket(WiFiUDP & udp, IPAddress &address);

time_t getNtpTime(WiFiUDP & udp)
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(udp, ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(WiFiUDP & udp, IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}
