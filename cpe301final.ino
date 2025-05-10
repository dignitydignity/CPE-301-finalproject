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
31 VENT IN1 AUTOCONFIG
33 VENT IN2 AUTOCONFIG
35 VENT IN3 AUTOCONFIG
37 VENT IN4 AUTOCONFIG
39 GREEN IDLE LIGHT PG2

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
Stepper myStepper = Stepper(stepsPerRev, 31, 33, 35, 37);

// UART Pointers
volatile unsigned char *myUCSR0A = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C = (unsigned char *)0x00C2;
volatile unsigned int  *myUBRR0  = (unsigned int *) 0x00C4;
volatile unsigned char *myUDR0   = (unsigned char *)0x00C6;
// GPIO Pointers
//since the LCD library sets itself up this might just need the status LEDs and buttons
volatile unsigned char *portA = (unsigned char *)0x20;
volatile unsigned char *portDDRA = (unsigned char *)0x19;
volatile unsigned char *portB = (unsigned char *)0x25;
volatile unsigned char *portDDRB = (unsigned char *)0x24;
volatile unsigned char *portD = (unsigned char *)0x2B;
volatile unsigned char *portDDRD = (unsigned char *)0x2A;
volatile unsigned char *portG = (unsigned char *)0x34;
volatile unsigned char *portDDRG = (unsigned char *)0x33;
volatile unsigned char *portE = (unsigned char *)0x2E;
volatile unsigned char *portDDRE = (unsigned char *)0x2D;

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
  //OUTPUTS: PB7,PA1,PA3,PA5,PG2
  //setting outputs, setting them low for start
  *portDDRA |= 0x2A;
  *portDDRB |= 0x80;
  *portDDRG |= 0x04;

  *portA &= ~0x2A;
  *portB &= ~0x80; 
  *portG &= ~0x04;

  //set inputs with pullup resistors
  *portDDRD &= ~0x0C;
  *portDDRE &= ~0x04;

  *portD |= 0x0C;
  *portE |= 0x04;

  // Timer setup
  // setup the Timer for Normal Mode, with the TOV interrupt enabled
  setup_timer_regs();
  // Start the UART
  U0Init(9600);

  //it took longer than I'd have liked to find out you have to manually tell this thing in another program that it is not january 1st 2000
  rtc.begin();

  //attach interrupts

  attachInterrupt(digitalPinToInterrupt(18), startPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(19), ventPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(2), resetPressed, FALLING);
  // LCD setup
  lcd.begin(16, 2);
  //turn off the fan
  fanToggle(0);
  state = 'D';

  myStepper.setSpeed(15);

  //adc init
  adc_init();
  //Serial.println("Setup complete");

}

//"The vent position should be adjustable all states except Disabled"
//fan adjust will be an interrupt why not
//if(state == "D"){} bit so the interrupt just doesn't do anything if called outside of the disabled state
//For the real time clock
unsigned long oldMillis = 0;
const long interval = 60000;

//toggles between 0 and not 0 so the fan direction toggles when pressed
volatile bool ventDir = 0;
volatile bool ventTurn = 0;

volatile unsigned int temp;
volatile unsigned int humidity;
volatile unsigned int water;
const int tempThresh = 25;
const int waterThresh = 50;  
volatile bool changedState = 0;

void loop() {
  //Serial.println("Main loop");
  if(changedState){
    printNow();
    char msg[] = "State changed to: ";
    int strlength = sizeof(msg) / sizeof(msg[0]);
    for(int i = 0; i < strlength-1; i++){
      U0putchar(msg[i]);
    }
    U0putchar(state);
    U0putchar('\n');
    if(state != 'D' && state != 'E'){
      //forces the thing to update when you go to the idle state even if it hasn't been a full minute
      readings();
      notify();
    }
    changedState = 0;
  }
  if(ventTurn){
    printNow();
    char msg[] = "Vent position toggled";
    int strlength = sizeof(msg) / sizeof(msg[0]);
    for(int i = 0; i < strlength-1; i++){
      U0putchar(msg[i]);
    }
    U0putchar('\n');
    myStepper.step(((stepsPerRev/8) * (-1 ^ ventDir)));
    ventTurn = 0;
  }
  
  switch (state) {
    case 'D':
      //Turn yellow LED on, all others off
      *portG &= ~0x04;
      *portA &= ~0x28;
      *portB |= 0x80;
      fanToggle(0);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("STANDBY");

      break;

    case 'I':
      //Turn on green LED, all others off
      *portB &= ~0x80;
      *portA &= ~0x28;
      *portG |= 0x04;
      //fan is off
      //Serial.println("idle"); //also for troubleshooting
      fanToggle(0);

      //Regularly read temp and humidity, display to LCD every minute
      readings();
      if((millis() - oldMillis) > interval){
        notify();
        oldMillis = millis();
      }
      
      //state gets changed to error if water level is too low
      if (water < waterThresh) {
        state = 'E';
        changedState=1;
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("LOW WATER");
      }

      //if temperature gets too high switch to running
      if (temp >= tempThresh) {
        changedState=1;
        state = 'R';
      }
      break;

    case 'E':
      *portB &= ~0x80;
      *portA &= ~0x20;
      *portG &= ~0x04;
      *portA |= 0x08;
      fanToggle(0);
      readings();
      break;

    case 'R':
      *portB &= ~0x80;
      *portA &= ~0x08;
      *portA |= 0x20;
      *portG &= ~0x04;
      //Turn on fan
      fanToggle(1);
      //Blue LED on, all others off

      //readings to LCD
      readings();
      if((millis() - oldMillis) > interval){
      notify();
      }

      //Turn state back to idle if temperature is below threshold
      if (temp < tempThresh) {
        state = 'I';
        changedState=1;
      }

      //Turn state to error if water gets too low
      if (water < waterThresh) {
        state = 'E';
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

void notify() {
    //Serial.println("Displaying data to LCD");
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
}

void ventPressed() {
  if(state != 'D'){
  ventDir ? ventDir = 0 : ventDir = 1;
  ventTurn = 1;
  }
} 

void resetPressed(){
  if(state == 'E'){
    state = 'I';
    changedState = 1;
  } 
}

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

void U0putchar(unsigned char U0pdata) {
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
    //hmm today I will find out this interferes with the rest of the A register if it just XORs everything
    *portA ^= 0x02;
  }
}

void printNow(){
  DateTime now = rtc.now();
  unsigned int hours2 = (now.hour() % 10);
  unsigned int hours1 = ((now.hour() - hours2) / 10);
  unsigned int minutes2 = (now.minute() % 10);
  unsigned int minutes1 = ((now.minute() - minutes2) / 10);
  unsigned int seconds2 = (now.second() % 10);
  unsigned int seconds1= ((now.second() - seconds2) / 10);

    U0putchar(hours1+48);
    U0putchar(hours2+48);
    U0putchar(':');
    U0putchar(minutes1+48);
    U0putchar(minutes2+48);
    U0putchar(':');
    U0putchar(seconds1+48);
    U0putchar(seconds2+48);
    U0putchar(' ');
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
        // stop the timer
        *myTCCR1B &= ~0x05;
        // set the flag to not running
        timer_running = 0;
        *portA &= ~0x02;
      }
    }
    else{
      
          currentTicks = 10204;
          if(!timer_running)
          {
              // start the timer
              *myTCCR1B |= 0x05;
              // set the running flag
              timer_running = 1;
          }
    }
}