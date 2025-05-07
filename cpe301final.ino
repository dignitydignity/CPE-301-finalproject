//Allen Bruan, Jackson Blake

//As of right now pulling relevant bits of code from labs, will clear up superfluous stuff at some later point

//yeah I'm not sure what to use the built in timer for, since monitoring is going to be done with the external real time clock

/*
PIN MAPPING

0
1 
2 LCD D4
3 LCD D5
4 LCD D6
5 LCD D7
6 MOTOR 1N1 
7 MOTOR 1N2
8 MOTOR 1N3
9 MOTOR 1N4
10 TEMP/HUMIDITY SENSOR
11 LCD RS
12 LCD EN
13 YELLOW DISABLED LED PB7
14 GREEN IDLE LED  PJ1
15 RED ERROR LED PJ0
16 BLUE RUNNING LED PH1
17 RESET BUTTON PH0
18 START BUTTON PD3
19 FAN BUTTON PD2
20 RTC SDA
21 RTC 21

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
//4096 steps per revolution
Stepper myStepper = Stepper(4096,6,7,8,9);

// UART Pointers
volatile unsigned char *myUCSR0A  = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B  = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C  = (unsigned char *)0x00C2;
volatile unsigned int  *myUBRR0   = (unsigned int *)0x00C4;
volatile unsigned char *myUDR0    = (unsigned char *)0x00C6;
// GPIO Pointers
//since the LCD library sets itself up this might just need the status LEDs and buttons
volatile unsigned char *portB  = (unsigned char *)0x25;
volatile unsigned char *portDDRB  = (unsigned char *)0x24;
volatile unsigned char *portJ  = (unsigned char *)0x105;
volatile unsigned char *portDDRJ  = (unsigned char *)0x104;
volatile unsigned char *portH  = (unsigned char *)0x102;
volatile unsigned char *portDDRH  = (unsigned char *)0x101;
volatile unsigned char *pinH  = (unsigned char *)0x100;
volatile unsigned char *portD  = (unsigned char *)0x2B;
volatile unsigned char *portDDRD  = (unsigned char *)0x2A;
volatile unsigned char *pinD  = (unsigned char *)0x29;
// Timer Pointers
volatile unsigned char *myTCCR1A  = (unsigned char *)0x80;
volatile unsigned char *myTCCR1B  = (unsigned char *)0x81;
volatile unsigned char *myTCCR1C  = (unsigned char *)0x82;
volatile unsigned char *myTIMSK1  = (unsigned char *)0x6F;
volatile unsigned char *myTIFR1   = (unsigned char *)0x36;
volatile unsigned int  *myTCNT1   = (unsigned int *)0x84;
// ADC pointers
volatile unsigned char* my_ADMUX = (unsigned char*) 0x7C;
volatile unsigned char* my_ADCSRB = (unsigned char*) 0x7B;
volatile unsigned char* my_ADCSRA = (unsigned char*) 0x7A;
volatile unsigned int* my_ADC_DATA = (unsigned int*) 0x78;
// LCD stuff
const int RS = 11, EN = 12, D4 = 2, D5 = 3, D6 = 4, D7 = 5;
LiquidCrystal lcd(RS, EN, D4, D5, D6, D7);

//global UART ticks counter
unsigned int currentTicks = 65535;
unsigned char timer_running = 0;

//made volatile because interrupts are going to be messing with it
volatile unsigned char state = 'D';
//or specifically the start button will be messing with it.
//Disabled, Idle, Error, Running, D,I,E,R, starts on disabled

void setup() 
{
  // GPIO setup
  //PB7, PJ1, PJ0, PH1 to output (status LEDs)
  //PH0, PD3, PD2 to input (control buttons)
  //setting outputs, setting them low for start
  *portDDRB |= 0x80;
  *portDDRJ |= 0x03;
  *portDDRH |= 0x02;

  *portB &= ~0x80;
  *portJ &= ~0x03;
  *portH &= ~0x02;

  //set inputs with pullup resistors
  *portDDRH &= ~0x01;
  *portDDRD &= ~0x0C;

  *portH |= 0x01;
  *portD |= 0x0C;

  // Timer setup
  // setup the Timer for Normal Mode, with the TOV interrupt enabled
  setup_timer_regs();
  // Start the UART
  U0Init(9600);

  // LCD setup
  lcd.begin(16,2);
}

//"The vent position should be adjustable all states except Disabled"
//fan adjust will be an interrupt why not
//if(state = "D"){} bit so the interrupt just doesn't do anything if called outside of the disabled state

//For the real time clock
unsigned long oldMillis = 0;
const long interval = 60000;


//I would put the ADC stuff in the main loop but apparently it isn't even allowed to monitor in the disabled state so that's also being
//moved into global variables and a different function
unsigned int temp;
unsigned int humidity;
unsigned int water;
const int tempThresh = 25;
const int waterThresh = 300; //calibrate later idk

void loop() 
{
  unsigned long newMillis = millis(); //
  switch(state){
      case 'D':
      //fan is off
      //Turn yellow LED on, all others off
      *portB |= 0x80;
      *portJ &= ~0x03;
      *portH &= ~0x02;


      //this state shouldn't watch for the start button, that's an interrupt's job according to project doc
      //so really I guess this state does nothing except for the fan and lights
    break;

    case 'I':
      //fan is off
      //Turn on green LED, all others off
      *portB &= ~0x80;
      *portJ |= 0x02;
      *portJ &= ~0x01;
      *portH &= ~0x02;

      //Regularly read temp and humidity, display to LCD
      readings();
      notify(newMillis);
      //state gets changed to error if water level is too low
      if(water < waterThresh){
        state = 'E';
      }

      //if temperature gets too high switch to running
      if(temp > tempThresh){
        state = 'R';
      }
      
    break;

    case 'E':
      //Ensure motor is off
      //Red LED on, all others off
      *portB &= ~0x80;
      *portJ &= ~0x02;
      *portJ |= 0x01;
      *portH &= ~0x02;
      
      //Reset button switches state back to idle, but only if the water level is above the minimum threshold
      readings();
      //uncomment this after figuring out where the reset button goes
      if(*pinD & 0x01 && water > waterThresh){
        state = "I";
        break;
      }

      //Display error message to LCD
      //Doc says readings should be displayed to the LCD in all states except disabled, also says error should display an error message
      //I feel like the error message takes precedence
      //actually will use the UART to make it alternate between the two perhaps
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("LOW WATER, REFILL");
      lcd.setCursor(0,1);
      lcd.print("THEN HIT RESET");
    break;

    case 'R':
      //Turn on fan
      //Blue LED on, all others off
      *portB &= ~0x80;
      *portJ &= ~0x03;
      *portH |= 0x02;
      //readings to LCD
      readings();
      notify(newMillis);

      //Turn state back to idle if temperature is below threshold
      if(temp < tempThresh){
        state = "I";
      }

      //Turn state to error if water gets too low
      if(water < waterThresh){
        state = "E";
      }
    break;
  }
}


// Timer setup function
void setup_timer_regs()
{
  // setup the timer control registers
  *myTCCR1A= 0x80;
  *myTCCR1B= 0X81;
  *myTCCR1C= 0x82;
  
  // reset the TOV flag
  *myTIFR1 |= 0x01;
  
  // enable the TOV interrupt
  *myTIMSK1 |= 0x01;
}


// TIMER OVERFLOW ISR
ISR(TIMER1_OVF_vect)
{
  // Stop the Timer
  *myTCCR1B &=0x00;
  // Load the Count
  *myTCNT1 =  (unsigned int) (65535 -  (unsigned long) (currentTicks));
  // Start the Timer
  *myTCCR1B |=   0x01;
  // if it's not the STOP amount
  if(currentTicks != 65535)
  {
    // XOR to toggle PB6
    *portB ^= 0x40;
  }
}
/*
 * UART FUNCTIONS
 */
void U0Init(int U0baud)
{
 unsigned long FCPU = 16000000;
 unsigned int tbaud;
 tbaud = (FCPU / 16 / U0baud - 1);
 *myUCSR0A = 0x20;
 *myUCSR0B = 0x18;
 *myUCSR0C = 0x06;
 *myUBRR0  = tbaud;
}
unsigned char kbhit()
{
  return (UCSR0A & 0x80);
}
unsigned char getChar()
{
  return *myUDR0;
}
void putChar(unsigned char U0pdata)
{
  while(!(UCSR0A & 0x20)){}
    *myUDR0 = U0pdata;
}

void adc_init()
{
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

unsigned int adc_read(unsigned char adc_channel_num)
{
  // clear the channel selection bits (MUX 4:0)
 * my_ADMUX &= ~0x0F;

  // clear the channel selection bits (MUX 5)
  *my_ADCSRB &= ~0x08;
 
  // set the channel selection bits for channel
  *my_ADMUX |= (0x01 << adc_channel_num);

  // set bit 6 of ADCSRA to 1 to start a conversion
  *my_ADCSRA |= 0x40;
  // wait for the conversion to complete
  while((*my_ADCSRA & 0x40) != 0);
  // return the result in the ADC data register and format the data based on right justification (check the lecture slide)
  
  unsigned int val = *my_ADC_DATA & 0x03FF;
  return val;
}

void readings(){
  water = adc_read(0);
  temp = DHT.temperature;
  humidity = DHT.humidity;
}

//checks if it's been a minute (or more) since the last update, if so writes the temperature and humidity to the LCD
//does nothing if not
void notify(long now){
  if(now - oldMillis >= interval){
    oldMillis = now;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Temp: ");
    lcd.print(temp);
    lcd.setCursor(0,1);
    lcd.print("Humidity: ");
    lcd.print(humidity);
    lcd.print("%");
  }
}
