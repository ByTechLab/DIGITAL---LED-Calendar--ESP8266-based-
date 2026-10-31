#include <ESP8266WiFi.h>
#include <include/WiFiState.h>
#include <time.h>
#include <coredecls.h>   // settimeofday_cb(), SNTP delay hooks
#include <sntp.h>        // sntp_stop()

// Requires ESP8266 Arduino core >= 3.0.0. The NTPClient library is no longer used.

// ---------------- NTP / TIME ---------------->
// Replace with your network credentials
const char *ssid     = "YOUR_SSID";
const char *password = "YOUR_PASSWD";

// POSIX timezone string - DST is handled automatically.
// Poland / Central Europe: CET (UTC+1) in winter, CEST (UTC+2) in summer,
// switching on the last Sunday of March at 02:00 and the last Sunday of October at 03:00.
// Other examples: "GMT0BST,M3.5.0/1,M10.5.0" (UK), "EST5EDT,M3.2.0,M11.1.0" (US East)
#define NTP_TZ      "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER  "pool.ntp.org"

// WiFi is OFF except during a sync. A sync is done:
//  * at boot (retried until it succeeds),
//  * once a day shortly before midnight, so clock drift is minimal at the day rollover,
//  * as a fallback if no successful sync happened for > 25 h (e.g. pre-midnight sync failed).
#define NTP_PRE_MIDNIGHT_WINDOW_START_MIN 50            // window 23:50 ...
#define NTP_PRE_MIDNIGHT_WINDOW_END_MIN   59            // ... 23:58 (last attempt starts before 23:59)
#define NTP_MIN_RESYNC_GAP_MS     (60UL * 60UL * 1000UL)          // 1 h  - don't resync twice in one window
#define NTP_FALLBACK_INTERVAL_MS  (25UL * 60UL * 60UL * 1000UL)   // 25 h - overdue -> sync anytime
#define NTP_RETRY_WINDOW_MS       (2UL * 60UL * 1000UL)           // 2 min - retry inside pre-midnight window
#define NTP_RETRY_OTHER_MS        (15UL * 60UL * 1000UL)          // 15 min - retry outside window (saves power if router is down)
#define NTP_BOOT_RETRY_MS         (10UL * 1000UL)                 // 10 s  - retry at boot
#define WIFI_CONNECT_TIMEOUT_MS   (20UL * 1000UL)
#define NTP_RESPONSE_TIMEOUT_MS   (10UL * 1000UL)

volatile bool NTP_Sync_Received = false;   // set by SNTP callback
bool          NTP_Last_Attempt_Failed = false;
uint32_t      NTP_Last_Sync_Ms = 0;
uint32_t      NTP_Last_Attempt_Ms = 0;

// Background SNTP is stopped after every sync, but in case it ever runs,
// keep its own update interval long (core default is 1 h).
uint32_t sntp_update_delay_MS_rfc_not_less_than_15000 ()
{
  return 24UL * 60UL * 60UL * 1000UL; // 24 h
}

// Send the first SNTP request immediately after configTime() -> shorter WiFi on-time.
uint32_t sntp_startup_delay_MS_rfc_not_less_than_60000 ()
{
  return 0;
}
// ---------------- NTP / TIME ----------------<

// ---------------- LED CALENDAR ---------------->
#define LED_CALENDAR_MAX_BRIGHTNESS 230
#define LED_CALENDAR_MIN_BRIGHTNESS 254

#define PIN_LED_SCK 14
#define PIN_LED_SDOUT 13
#define PIN_LED_OE 12
#define PIN_LED_STORE_DAT 5
#define PIN_LED_MASTER_RST 4

#define SHIFT_REG_SERIAL_HALF_PERIOD 25 // 25us -> 20 kHz

unsigned int LED_Calendar_Month = 1;
unsigned int LED_Calendar_Day = 1;
unsigned int LED_Calendar_Year = 0;

unsigned int LED_Calendar_Month_old = 1;
unsigned int LED_Calendar_Day_old = 1;
unsigned int LED_Calendar_Year_old = 0;

uint8_t LED_Calendar_Year_Digit_LUT [10] = 
{
    0b11111100, // 0
    0b01100000, // 1
    0b11011010, // 2
    0b11110010, // 3
    0b01100110, // 4
    0b10110110, // 5
    0b10111110, // 6
    0b11100000, // 7
    0b11111110, // 8
    0b11110110, // 9
};
// ---------------- LED CALENDAR ----------------<

void setup() 
{
  // Initialize Serial Monitor
  Serial.begin(115200);

  Serial.println("<--------- LED CALENDAR STARTUP --------->");

  // Don't write WiFi config to flash on every connect, and keep radio off until needed
  WiFi.persistent(false);
  WiFi_Sleep();

  LED_Calendar_Setup();
  LED_Calendar_Check_Ambient_Light();

  LED_Calendar_Clear_All_Leds();
  LED_Calendar_Startup_Animation();
  LED_Calendar_Clear_All_Leds();

  NTP_Setup();
  NTP_Update_Time_Variables();
}

void loop()
{
  // Network sync only when due (rare). Otherwise returns immediately.
  NTP_Handle_Sync();

  // Date from the local clock - no network, checked every loop (~2 s)
  NTP_Update_Time_Variables();
  
  LED_Calendar_Check_Ambient_Light();

  if( LED_Calendar_Month != LED_Calendar_Month_old || LED_Calendar_Day != LED_Calendar_Day_old || LED_Calendar_Year != LED_Calendar_Year_old )
  {
    Serial.print("New date received --> Updating LEDs, local time: ");
    NTP_Print_Local_Time();
    LED_Calendar_Write_Date_To_LEDs_Animation();

    LED_Calendar_Month_old = LED_Calendar_Month;
    LED_Calendar_Day_old = LED_Calendar_Day;
    LED_Calendar_Year_old = LED_Calendar_Year;
  }

  
  delay(1000);
  LED_Calendar_Write_Date_Blink_Last_LED(1);
  delay(1000);
  LED_Calendar_Write_Date_Blink_Last_LED(0);
}

// ******************************************************************************************
// ******************************************************************************************

// Called by the core whenever system time is set
void NTP_Time_Set_Callback (bool from_sntp)
{
  if (from_sntp)
  {
    NTP_Sync_Received = true;
  }
}

void WiFi_Sleep (void)
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);
}

bool WiFi_Wake_And_Connect (void)
{
  WiFi.forceSleepWake();
  delay(1);
  WiFi.mode(WIFI_STA);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    if ((millis() - start) >= WIFI_CONNECT_TIMEOUT_MS)
    {
      Serial.println(" WiFi connect timeout");
      return false;
    }
    delay(100);
    Serial.print(".");
  }
  Serial.println(" connected");
  return true;
}

// One complete sync: WiFi on -> SNTP request -> wait for answer -> SNTP stop -> WiFi off
bool NTP_Sync_Once (void)
{
  NTP_Last_Attempt_Ms = millis();
  bool ok = false;

  if (WiFi_Wake_And_Connect())
  {
    NTP_Sync_Received = false;
    configTime(NTP_TZ, NTP_SERVER);   // (re)starts SNTP -> immediate request

    uint32_t start = millis();
    while (!NTP_Sync_Received && (millis() - start) < NTP_RESPONSE_TIMEOUT_MS)
    {
      delay(100);
    }
    sntp_stop();                      // system clock keeps running locally
    ok = NTP_Sync_Received;
  }

  WiFi_Sleep();

  if (ok)
  {
    NTP_Last_Sync_Ms = millis();
    NTP_Last_Attempt_Failed = false;
    Serial.print("NTP sync OK, local time: ");
    NTP_Print_Local_Time();
  }
  else
  {
    NTP_Last_Attempt_Failed = true;
    Serial.println("NTP sync FAILED");
  }
  return ok;
}

void NTP_Setup (void)
{
  settimeofday_cb(NTP_Time_Set_Callback);
  setenv("TZ", NTP_TZ, 1);
  tzset();

  // Don't show a 1970 date - block until the first valid time
  while (!NTP_Sync_Once())
  {
    Serial.println("Retrying first NTP sync in 10 s");
    delay(NTP_BOOT_RETRY_MS);
  }
}

// Decide whether a sync is due. Cheap when nothing to do.
void NTP_Handle_Sync (void)
{
  uint32_t now_ms = millis();

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  bool in_pre_midnight_window = (t.tm_hour == 23) &&
                                (t.tm_min >= NTP_PRE_MIDNIGHT_WINDOW_START_MIN) &&
                                (t.tm_min <  NTP_PRE_MIDNIGHT_WINDOW_END_MIN);

  if (NTP_Last_Attempt_Failed)
  {
    uint32_t retry_ms = in_pre_midnight_window ? NTP_RETRY_WINDOW_MS : NTP_RETRY_OTHER_MS;
    if ((now_ms - NTP_Last_Attempt_Ms) < retry_ms)
    {
      return;
    }
  }

  uint32_t since_sync_ms = now_ms - NTP_Last_Sync_Ms;
  bool synced_recently = since_sync_ms < NTP_MIN_RESYNC_GAP_MS;
  bool overdue         = since_sync_ms >= NTP_FALLBACK_INTERVAL_MS;

  if ((in_pre_midnight_window && !synced_recently) || overdue)
  {
    NTP_Sync_Once();
  }
}

void NTP_Print_Local_Time (void)
{
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &t);
  Serial.println(buf);
}

void LED_Calendar_Setup (void)
{
  pinMode(PIN_LED_SCK, OUTPUT);
  pinMode(PIN_LED_STORE_DAT, OUTPUT);

  pinMode(PIN_LED_SDOUT, OUTPUT);
  pinMode(PIN_LED_OE, OUTPUT);
  pinMode(PIN_LED_MASTER_RST, OUTPUT);

  //--------------------------------

  digitalWrite(PIN_LED_MASTER_RST, HIGH);

  analogWriteFreq(20000); // 20 kHz
  analogWrite(PIN_LED_OE, 254); // init PWM to lowest brightness
}


// Reads the date from the local system clock (kept by the core between syncs)
void NTP_Update_Time_Variables(void)
{
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  LED_Calendar_Day   = t.tm_mday;
  LED_Calendar_Month = t.tm_mon + 1;
  LED_Calendar_Year  = t.tm_year + 1900;
}

void LED_Calendar_Write_Date_To_LEDs_Animation (void)
{
  unsigned int number_of_day_led_target = (((LED_Calendar_Month-1)*32)+LED_Calendar_Day);

  unsigned int calendar_year_digits [4] = 
  {
    (LED_Calendar_Year % 10),
    (LED_Calendar_Year / 10) % 10,
    (LED_Calendar_Year / 100) % 10,
    (LED_Calendar_Year / 1000) % 10
  };

  for (unsigned int number_of_day_led = 0; number_of_day_led <= number_of_day_led_target; number_of_day_led++ )
  {
    for (unsigned int ii = 0 ; ii < 4 ; ii++)
    {
      uint8_t single_digit_bits = LED_Calendar_Year_Digit_LUT [calendar_year_digits[ii]];
      for (unsigned int i = 0 ; i < 8 ; i++)
      {
        
        if ((single_digit_bits & 0b00000001) == 0b00000001)
        {
          digitalWrite(PIN_LED_SDOUT, HIGH);
        }
        else
        {
          digitalWrite(PIN_LED_SDOUT, LOW);
        }
        digitalWrite(PIN_LED_SCK, LOW);
        yield();
        delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
        digitalWrite(PIN_LED_SCK, HIGH);
        yield();
        delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD); 
        single_digit_bits = single_digit_bits >> 1;
      }
    }

    // Fill days with 0's
    digitalWrite(PIN_LED_SDOUT, LOW);
    for (unsigned int i = 0 ; i < (384-number_of_day_led) ; i++)
    {
      digitalWrite(PIN_LED_SCK, LOW);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
      digitalWrite(PIN_LED_SCK, HIGH);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD); 
    }

    // Fill days with 1's
    digitalWrite(PIN_LED_SDOUT, HIGH);
    for (unsigned int i = 0 ; i < number_of_day_led ; i++)
    {
      digitalWrite(PIN_LED_SCK, LOW);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
      digitalWrite(PIN_LED_SCK, HIGH);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
    }

    digitalWrite(PIN_LED_SDOUT, LOW);

    // Latch data
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);                      
    digitalWrite(PIN_LED_STORE_DAT, LOW); 
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD); 
    digitalWrite(PIN_LED_STORE_DAT, HIGH);
  }
}

void LED_Calendar_Write_Date_Blink_Last_LED (unsigned int last_led_onoff)
{
  unsigned int number_of_day_led = (((LED_Calendar_Month-1)*32)+LED_Calendar_Day) - last_led_onoff;

  unsigned int calendar_year_digits [4] = 
  {
    (LED_Calendar_Year % 10),
    (LED_Calendar_Year / 10) % 10,
    (LED_Calendar_Year / 100) % 10,
    (LED_Calendar_Year / 1000) % 10
  };

  for (unsigned int ii = 0 ; ii < 4 ; ii++)
  {
    uint8_t single_digit_bits = LED_Calendar_Year_Digit_LUT [calendar_year_digits[ii]];
    for (unsigned int i = 0 ; i < 8 ; i++)
    {
      
      if ((single_digit_bits & 0b00000001) == 0b00000001)
      {
        digitalWrite(PIN_LED_SDOUT, HIGH);
      }
      else
      {
        digitalWrite(PIN_LED_SDOUT, LOW);
      }
      digitalWrite(PIN_LED_SCK, LOW);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
      digitalWrite(PIN_LED_SCK, HIGH);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD); 
      single_digit_bits = single_digit_bits >> 1;
    }
  }

  // Fill days with 0's
  digitalWrite(PIN_LED_SDOUT, LOW);
  for (unsigned int i = 0 ; i < (384-number_of_day_led) ; i++)
  {
    digitalWrite(PIN_LED_SCK, LOW);
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
    digitalWrite(PIN_LED_SCK, HIGH);
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD); 
  }

  // Fill days with 1's
  digitalWrite(PIN_LED_SDOUT, HIGH);
  for (unsigned int i = 0 ; i < number_of_day_led ; i++)
  {
    digitalWrite(PIN_LED_SCK, LOW);
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
    digitalWrite(PIN_LED_SCK, HIGH);
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
  }

  digitalWrite(PIN_LED_SDOUT, LOW);

  // Latch data
  yield();
  delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);                      
  digitalWrite(PIN_LED_STORE_DAT, LOW); 
  yield();
  delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD); 
  digitalWrite(PIN_LED_STORE_DAT, HIGH);
  
}

void LED_Calendar_Check_Ambient_Light (void)
{
  unsigned int ambient_light_intensity = analogRead(A0);
  analogWrite(PIN_LED_OE, map(ambient_light_intensity,0,600,LED_CALENDAR_MIN_BRIGHTNESS,LED_CALENDAR_MAX_BRIGHTNESS));
  Serial.print("Ambient Light Intensity = ");
  Serial.println(ambient_light_intensity);
}

void LED_Calendar_Startup_Animation (void)
{
  for ( unsigned int num_of_on_leds = 0 ; num_of_on_leds < 416 ; num_of_on_leds++)
  {
    // Fill days with 0's
    digitalWrite(PIN_LED_SDOUT, LOW);
    for (unsigned int i = 0 ; i < (416-num_of_on_leds) ; i++)
    {
      digitalWrite(PIN_LED_SCK, LOW);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
      digitalWrite(PIN_LED_SCK, HIGH);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
    }

    // Fill days with 1's
    digitalWrite(PIN_LED_SDOUT, HIGH);
    for (unsigned int i = 0 ; i < num_of_on_leds ; i++)
    {
      digitalWrite(PIN_LED_SCK, LOW);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
      digitalWrite(PIN_LED_SCK, HIGH);
      yield();
      delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
    }

    digitalWrite(PIN_LED_SDOUT, LOW);

    // Latch data
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);                     
    digitalWrite(PIN_LED_STORE_DAT, LOW); 
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);                     
    digitalWrite(PIN_LED_STORE_DAT, HIGH); 
  }
}

void LED_Calendar_Clear_All_Leds (void)
{
  // Fill days with 0's
  digitalWrite(PIN_LED_SDOUT, LOW);
  for (unsigned int i = 0 ; i < 416 ; i++)
  {
    digitalWrite(PIN_LED_SCK, LOW);
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
    digitalWrite(PIN_LED_SCK, HIGH);
    yield();
    delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);  
  }

  digitalWrite(PIN_LED_SDOUT, LOW);

  // Latch data
  yield();
  delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);                     
  digitalWrite(PIN_LED_STORE_DAT, LOW); 
  yield();
  delayMicroseconds(SHIFT_REG_SERIAL_HALF_PERIOD);                     
  digitalWrite(PIN_LED_STORE_DAT, HIGH); 
  
}
