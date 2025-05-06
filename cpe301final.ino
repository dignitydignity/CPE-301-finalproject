//Allen Bruan, Jackson Blake

//As of right now pulling relevant bits of code from labs, will clear up superfluous stuff at some later point

#include <LiquidCrystal.h>

#define RDA
#define TBE 

// UART Pointers
volatile unsigned char *myUCSR0A  = (unsigned char *)0x00C0;
volatile unsigned char *myUCSR0B  = (unsigned char *)0x00C1;
volatile unsigned char *myUCSR0C  = (unsigned char *)0x00C2;
volatile unsigned int  *myUBRR0   = (unsigned int *)0x00C4;
volatile unsigned char *myUDR0    = (unsigned char *)0x00C6;
// GPIO Pointers
volatile unsigned char *portB     = (unsigned char *)0x25;
volatile unsigned char *portDDRB  = (unsigned char *)0x24;
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


//byte in_char; //copied from lab8, don't think it'll be really useful at the moment but won't delete it until I'm 100% sure
//This array holds the tick values
//calculate all the tick value for the given frequencies and put that in the ticks array
//unsigned int ticks[7]= {18182,16194,15296,13629,12140,11461,10204};
//This array holds the characters to be entered, index echos the index of the ticks
//that means ticks[0] should have the tick value for the character in input[0]
//unsigned char input[7]= {'A','B','C','D','E','F','G'};

//global ticks counter
unsigned int currentTicks = 65535;
unsigned char timer_running = 0;

unsigned int char state = D;
//Disabled, Idle, Error, Running, D,I,E,R, starts on disabled

void setup() 
{                
  // set PB6 to output
  *portDDRB |= 0x40;
  // set PB6 LOW
  *portB &= ~0x40;
  // setup the Timer for Normal Mode, with the TOV interrupt enabled
  setup_timer_regs();
  // Start the UART
  U0Init(9600);
}

//"The vent position should be adjustable all states except Disabled"
//Start and fan adjust positions will both be buttons connected to pins with interrupts, both can be interrupts, why not
//Could have the disable state just outright disable the reset button pin's interrupt capability, but I feel it'd be
//quicker to just have an if(state = "D"){} bit so the interrupt just doesn't do anything if called outside of the disabled state

void loop() 
{
  switch(state){
    case "D":
    //fan is off
    //Turn yellow LED on, all others off

    //this state shouldn't watch for the start button, that's an interrupt's job according to project doc

    //
    break;

    case "I":
    //fan is off
    //Turn on green LED, all others off
    
    //Regularly read temp and humidity, display to LCD

    //state gets changed to error if water level is too low
    break;

    case "E":
    //Ensure motor is off
    //Red LED on, all others off
    
    //Reset button switches state back to idle, but only if the water level is above the minimum threshold

    //Display error message to LCD
    break;

    case "R":
    //Turn on fan
    //Blue LED on, all others off

    //Turn state back to idle if temperature is below threshold

    //Turn state to error if water gets too low
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
