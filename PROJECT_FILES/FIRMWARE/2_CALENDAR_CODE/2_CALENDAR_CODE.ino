#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <include/WiFiState.h>

// -------- TODO -------->
// * Reduce NTP request frequency - day change prediction algorithm,
// * Compile & Libraries & Upload instructions,

// ---------------- NTP ---------------->
// Replace with your network credentials
const char *ssid     = "YOUR_SSID";
const char *password = "YOUR_PASSWD";
// Set offset time in seconds to adjust for your timezone, for example:
// GMT +1 = 3600
// GMT +8 = 28800
// GMT -1 = -3600
// GMT 0 = 0
const unsigned int NTP_Timezone_Offset = 7200; //(GMT +2)

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

unsigned int NTP_Check_Counter = 0;
#define NTP_CHECK_EVERY_N_LOOPS 30 // 60 seconds
// ---------------- NTP ----------------<

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
  if (NTP_Check_Counter >= NTP_CHECK_EVERY_N_LOOPS)
  {
    NTP_Update_Time_Variables();
    NTP_Check_Counter = 0;
  }
  else
  {
    NTP_Check_Counter++;
  }
  
  LED_Calendar_Check_Ambient_Light();

  if( LED_Calendar_Month != LED_Calendar_Month_old || LED_Calendar_Day != LED_Calendar_Day_old || LED_Calendar_Year != LED_Calendar_Year_old )
  {
    Serial.println("New date received --> Updating LEDs");
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

void NTP_Setup (void)
{
  // Connect to Wi-Fi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  // Initialize a NTPClient to get time
  timeClient.begin();
  timeClient.setTimeOffset(NTP_Timezone_Offset);
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


void NTP_Update_Time_Variables(void)
{
  timeClient.update();

  time_t epochTime = timeClient.getEpochTime();
  Serial.print("Epoch Time: ");
  Serial.print(epochTime);

  int currentHour = timeClient.getHours();
  Serial.print(",Hour: ");
  Serial.print(currentHour);  

  int currentMinute = timeClient.getMinutes();
  Serial.print(",Minutes: ");
  Serial.print(currentMinute); 
   
  int currentSecond = timeClient.getSeconds();
  Serial.print(",Seconds: ");
  Serial.print(currentSecond); 

  //Get a time structure
  struct tm *ptm = gmtime ((time_t *)&epochTime); 

  LED_Calendar_Day = ptm->tm_mday;
  Serial.print(",Month day: ");
  Serial.print(LED_Calendar_Day);

  LED_Calendar_Month = ptm->tm_mon+1;
  Serial.print(",Month: ");
  Serial.print(LED_Calendar_Month);

  LED_Calendar_Year = ptm->tm_year+1900;
  Serial.print(",Year: ");
  Serial.println(LED_Calendar_Year);
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


