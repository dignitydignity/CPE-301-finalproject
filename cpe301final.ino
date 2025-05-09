//Allen Bruan, Jackson Blake

//wow I finally found a use for the UART as a fan controller

/*
PIN MAPPING
apparently plugging things into the communication pins messes with the serial monitor so I will no longer be doing that

2 RESET BUTTON PJ1
3 LCD D5 AUTOCONFIG
4 LCD D6 AUTOCONFIG
5 LCD D7 AUTOCONFIG
6 LCD D4 AUTOCONFIG
10 TEMP/HUMIDITY SENSOR AUTOCONFIG
11 LCD RS AUTOCONFIG
12 LCD EN AUTOCONFIG
13 YELLOW DISABLE LIGHT PB7
18 START/STOP BUTTON PD3
19 VENT BUTTON PD2
20 RTC SDA AUTOCONFIG
21 RTC SCL AUTOCONFIG
23 FAN OUTPUT PA1
25 RED ERROR LIGHT PA3
27 BLUE RUNNING LIGHT PA5
29 GREEN IDLE LIGHT PA7
31 VENT IN1 AUTOCONFIG
33 VENT IN2 AUTOCONFIG
35 VENT IN3 AUTOCONFIG
37 VENT IN4 AUTOCONFIG

//INPUTS: PJ1,PD2,PD3
//OUTPUTS: PB7,PA1,PA3,PA5,PA7

A0 WATER SENSOR
*/

#include <LiquidCrystal.h>
#include <dht.h>
#include <RTClib.h>
#include <Stepper.h>

#define RDA
#define TBE
#define DHT11PIN 10

dht DHT;
RTC_DS3231 rtc;
int stepsPerRev = 4096;
Stepper myStepper = Stepper(stepsPerRev, 6, 7, 8, 9);

// UART Pointers
volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int *myUBRR0 = (unsigned int *)0x00C4;
volatile unsigned char *myUDR0 = (unsigned char *)0x00C6;
// GPIO Pointers
//since the LCD library sets itself up this might just need the status LEDs and buttons
volatile unsigned char *portA = (unsigned char *)0x20;
volatile unsigned char *portDDRA = (unsigned char *)0x19;
volatile unsigned char *portB = (unsigned char *)0x25;
volatile unsigned char *portDDRB = (unsigned char *)0x24;
volatile unsigned char *portD = (unsigned char *)0x2B;
volatile unsigned char *portDDRD = (unsigned char *)0x2A;
volatile unsigned char *portJ = (unsigned char *)0x105;
volatile unsigned char *portDDRJ = (unsigned char *)0x104;

// Timer Pointers
volatile unsigned char *myTCCR1A = (unsigned char *)0x80;
volatile unsigned char *myTCCR1B = (unsigned char *)0x81;
volatile unsigned char *myTCCR1C = (unsigned char *)0x82;
volatile unsigned char *myTIMSK1 = (unsigned char *)0x6F;
volatile unsigned char *myTIFR1 = (unsigned char *)0x36;
volatile unsigned int *myTCNT1 = (unsigned int *)0x84;
// ADC pointers
volatile unsigned char *my_ADMUX = (unsigned char *)0x7C;
volatile unsigned char *my_ADCSRB = (unsigned char *)0x7B;
volatile unsigned char *my_ADCSRA = (unsigned char *)0x7A;
volatile unsigned int *my_ADC_DATA = (unsigned int *)0x78;
// LCD stuff
const int RS = 11, EN = 12, D4 = 6, D5 = 3, D6 = 4, D7 = 5;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

//global UART ticks counter
unsigned int currentTicks = 65535;
unsigned char timer_running = 0;

//made volatile because interrupts are going to be messing with it
volatile unsigned char state = 'D';
//or specifically the start button will be messing with it.
//Disabled, Idle, Error, Running, D,I,E,R, starts on disabled



void setup() {
  //Serial.println("Setting up");
  // GPIO setup
  //INPUTS: PJ1,PD2,PD3
  //OUTPUTS: PB7,PA1,PA3,PA5,PA7
  //setting outputs, setting them low for start
  *portDDRA |= 0xAA;
  *portDDRB |= 0x80;

  *portA &= ~0xAA;
  *portB &= ~0x80;

  // Timer setup
  // setup the Timer for Normal Mode, with the TOV interrupt enabled
  setup_timer_regs();
  // Start the UART
  U0Init(9600);

  rtc.begin();

  //adc init
  adc_init();

  //set inputs with pullup resistors
  *portDDRD &= ~0x0C;
  *portDDRJ &= ~0x02;

  *portD |= 0x0C;
  *portJ |= 0x02;

  //attach interrupts

  attachInterrupt(digitalPinToInterrupt(18), startPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(19), ventPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(2), resetPressed, FALLING);
  // LCD setup
  lcd.begin(16, 2);
  //turn off the fan
  fanToggle(0);
  state = 'D';
  Serial.println("Setup complete");

}

//"The vent position should be adjustable all states except Disabled"
//fan adjust will be an interrupt why not
//if(state == "D"){} bit so the interrupt just doesn't do anything if called outside of the disabled state

//For the real time clock
unsigned long oldMillis = 0;
const long interval = 60000;

//toggles between 0 and not 0 so the fan direction toggles when pressed
unsigned char fanDir = 0;

//I would put the ADC stuff in the main loop but apparently it isn't even allowed to monitor in the disabled state so that's also being
//moved into global variables and a different function
unsigned int temp;
unsigned int humidity;
unsigned int water;
const int tempThresh = 15;
const int waterThresh = 0;  //calibrate later idk
bool changedState = 0;
bool ventDir = 0;

void loop() {
  //Serial.println("Main loop");
  if(changedState){
    Serial.write("Changed state to ");
    Serial.write(state);
    Serial.write('\n');
  }
  unsigned char lastState = state;
  changedState = 0;
  switch (state) {
    case 'D':
    
      //fan is off
      fanToggle(0);
      //Serial.println("disabled"); //for troubleshooting
      /*currentTicks = 65535;
      *myTCCR1B &= 0xF8;
      timer_running = 0;
      *portE &= ~0x02;*/

      //Turn yellow LED on, all others off
      *portB |= 0x80;
      *portA &= ~0xA8;


      //this state shouldn't watch for the start button, that's an interrupt's job according to project doc
      //so really I guess this state does nothing except for the fan and lights
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("STANDBY");
      delay(250);

      break;

    case 'I':
      //fan is off
      //Serial.println("idle"); //also for troubleshooting
      fanToggle(0);
      //Turn on green LED, all others off
      *portB &= ~0x80;
      *portA &= ~0x28;
      *portA |= 0x80;

      //Regularly read temp and humidity, display to LCD
      readings();
      notify();
      //state gets changed to error if water level is too low
      if (water < waterThresh) {
        state = 'E';
        changedState=1;
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("LOW WATER");
      }

      //if temperature gets too high switch to running
      if (temp > tempThresh) {
        changedState=1;
        state = 'R';
      }
      break;

    case 'E':
      //Ensure motor is off
      fanToggle(0);
      //Red LED on, all others off
      *portB &= ~0x80;
      *portA &= ~0xA0;
      *portA |= 0x08;

      //Reset button switches state back to idle, but only if the water level is above the minimum threshold
      readings();
      //uncomment this after figuring out where the reset button goes

      //Display error message to LCD
      //Doc says readings should be displayed to the LCD in all states except disabled, also says error should display an error message
      // I feel like the error message takes precedence
      //actually will use the UART to make it alternate between the two perhaps
      break;

    case 'R':
      //Turn on fan
      fanToggle(1);
      //Blue LED on, all others off
      *portB &= ~0x80;
      *portA &= ~0x88;
      *portA |= 0x20;
      //readings to LCD
      if((millis() - oldMillis) > interval){
      readings();
      notify();
      }

      //Turn state back to idle if temperature is below threshold
      if (temp < tempThresh) {
        state = "I";
        changedState=1;
        /*DateTime stamp = rtc.now();
        char buffer[32];
        int len = sprintf(buffer, "%d:%d:%d: LO TEMP FAN OFF", stamp.hour(), stamp.minute(), stamp.second());
        for (int i = 0; i < len; i++) {
          putchar(buffer[i]);
        }
        putchar('\n');*/
      }

      //Turn state to error if water gets too low
      if (water < waterThresh) {
        state = "E";
        changedState=1;
      }
      break;
  }
}

void readings() {
  DHT.read11(10);
  water = adc_read(0);
  temp = DHT.temperature;
  humidity = DHT.humidity;
}

//checks if it's been a minute (or more) since the last update, if so writes the temperature and humidity to the LCD
//does nothing if not
void notify() {
    //Serial.println("Displaying data to LCD");
    oldMillis = millis();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(temp, DEC);
    lcd.setCursor(0, 1);
    lcd.print("Humidity: ");
    lcd.print(humidity, DEC);
    lcd.print("%");
}

void startPressed() {
  //it's a toggle button now
    state == 'D' ? state = 'I' : state = 'D';
    changedState = 1;
    /* DateTime stamp = rtc.now();
    char buffer[32];
    int len = sprintf(buffer, "%d:%d:%d: START PRESSED", stamp.hour(), stamp.minute(), stamp.second());
    for (int i = 0; i < len; i++) {
      putchar(buffer[i]);
    }
    putchar('\n');
  } else if (state != 'D') {
    state = 'D';
    DateTime stamp = rtc.now();
    char buffer[32];
    int len = sprintf(buffer, "%d:%d:%d: STOP PRESSED", stamp.hour(), stamp.minute(), stamp.second());
    for (int i = 0; i < len; i++) {
      putchar(buffer[i]);
    }
    putchar('\n');
  }*/
}

void ventPressed() {
  ventDir ? ventDir = 0 : ventDir = 1;
} 

void resetPressed(){
  if(state == 'E'){
    state = 'I';
    changedState = 1;
  }
}
/*  void toggleFan(bool running){
    if(running){
      currentTicks = 10204;
      if(!timer_running){
        *myTCCR1B |= 0x05;
        timer_running = 1;
      }
    } else{
      currentTicks = 65535;
      if(timer_running){
        *myTCCR1B &= 0xF8;
        timer_running = 0;
        *portE &= ~0x02;
      }
    }
  }
*/


//Functions from previous labs
void U0Init(int U0baud) {
  unsigned long FCPU = 16000000;
  unsigned int tbaud;
  tbaud = (FCPU / 16 / U0baud - 1);
  *myUCSR0A = 0x20;
  *myUCSR0B = 0x18;
  *myUCSR0C = 0x06;
  *myUBRR0 = tbaud;
}

unsigned char kbhit() {
  return (UCSR0A & 0x80);
}

unsigned char getChar() {
  return *myUDR0;
}

void putChar(unsigned char U0pdata) {
  while (!(UCSR0A & 0x20)) {}
  *myUDR0 = U0pdata;
}

void adc_init() {
  // setup the A register
  // set bit   7 to 1 to enable the ADC
  *my_ADCSRA |= 0x80;
  // clear bit 6 to 0 to disable the ADC trigger mode
  *my_ADCSRA &= ~0x40;
  // clear bit 5 to 0 to disable the ADC interrupt
  *my_ADCSRA &= ~0x20;
  // clear bit 0-2 to 0 to set prescaler selection to slow reading
  *my_ADCSRA &= ~0x07;
  // setup the B register
  // clear bit 3 to 0 to reset the channel and gain bits
  *my_ADCSRB &= ~0x08;
  // clear bit 2-0 to 0 to set free running mode
  *my_ADCSRB &= ~0x07;
  // setup the MUX Register
  // clear bit 7 to 0 for AVCC analog reference
  *my_ADMUX &= ~0x80;
  // set bit 6 to 1 for AVCC analog reference
  *my_ADMUX |= 0x40;
  // clear bit 5 to 0 for right adjust result
  *my_ADMUX &= ~0x20;
  // clear bit 4-0 to 0 to reset the channel and gain bits
  *my_ADMUX &= ~0x0F;
}

unsigned int adc_read(unsigned char adc_channel_num) {
  // clear the channel selection bits (MUX 4:0)
  *my_ADMUX &= ~0x0F;

  // clear the channel selection bits (MUX 5)
  *my_ADCSRB &= ~0x08;

  // set the channel selection bits for channel
  *my_ADMUX |= (0x01 << adc_channel_num);

  // set bit 6 of ADCSRA to 1 to start a conversion
  *my_ADCSRA |= 0x40;
  // wait for the conversion to complete
  while ((*my_ADCSRA & 0x40) != 0)
    ;
  // return the result in the ADC data register and format the data based on right justification (check the lecture slide)

  unsigned int val = *my_ADC_DATA & 0x03FF;
  return val;
}

// Timer setup function
void setup_timer_regs() {
  // setup the timer control registers
  *myTCCR1A = 0x80;
  *myTCCR1B = 0X81;
  *myTCCR1C = 0x82;

  // reset the TOV flag
  *myTIFR1 |= 0x01;

  // enable the TOV interrupt
  *myTIMSK1 |= 0x01;
}

// TIMER OVERFLOW ISR
ISR(TIMER1_OVF_vect) {
  // Stop the Timer
  *myTCCR1B &= 0x00;
  // Load the Count
  *myTCNT1 = (unsigned int)(65535 - (unsigned long)(currentTicks));
  // Start the Timer
  *myTCCR1B |= 0x01;
  // if it's not the STOP amount
  if (currentTicks != 65535) {
    // XOR to toggle PE1
    *portA ^= 0x01;
  }
}

//fan toggle
void fanToggle(bool status){
      if(!status)
    {
      
      // set the current ticks to the max value
      currentTicks = 65535;
      // if the timer is running
      if(timer_running)
      {
        Serial.write("Turned fan off");
        // stop the timer
        *myTCCR1B &= ~0x05;
        // set the flag to not running
        timer_running = 0;
        *portA &= ~0x01;
      }
    }
    else
    {
      
          currentTicks = 10204;
          if(!timer_running)
          {
            Serial.write("Turned fan on");
              // start the timer
              *myTCCR1B |= 0x05;
              // set the running flag
              timer_running = 1;
          }
    }
}