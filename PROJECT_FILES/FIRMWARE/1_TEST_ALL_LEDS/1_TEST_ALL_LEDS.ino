#define LED_SCK 14
#define LED_SDOUT 13
#define LED_OE 12
#define LED_STORE_DAT 5
#define LED_MASTER_RST 4



void setup() {
  pinMode(LED_SCK, OUTPUT);
  pinMode(LED_STORE_DAT, OUTPUT);

  pinMode(LED_SDOUT, OUTPUT);
  pinMode(LED_OE, OUTPUT);
  pinMode(LED_MASTER_RST, OUTPUT);

  //--------------------------------

  digitalWrite(LED_MASTER_RST, HIGH);

  // digitalWrite(LED_OE, HIGH);
  analogWriteFreq(10000);
  analogWrite(LED_OE, 254);

  digitalWrite(LED_SDOUT, HIGH);

  int i = 0;
    for ( i= 0 ; i<= 500 ; i++)
    {
      digitalWrite(LED_SCK, HIGH);
      delay(1);  
      digitalWrite(LED_SCK, LOW);
      delay(1);  
    }

  delay(10);                     
  digitalWrite(LED_STORE_DAT, LOW); 
  delay(10);                     
  digitalWrite(LED_STORE_DAT, HIGH); 
  
}

// the loop function runs over and over again forever
void loop() {


}
