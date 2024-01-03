/*
 * Project_Mega.c
 *
 * Created: 4/1/2023 9:08:37 PM
 * Author : Thomas
 */ 

#define F_CPU 16000000UL
#define FOSC 16000000UL // Clock Speed
#define BAUD 9600
#define MYUBRR (FOSC/16/BAUD-1)

#include <stdio.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/setbaud.h>
#include <stdlib.h>
#include <string.h>

#include "lcd.h" // lcd header file made by Peter Fleury
#include "keypad.h" // From course material


// Global Variables
int timeout = 0; // For noticing timeout 
int state = 0; // state 0 offline, state 1 active, state 2 change password, state 3 alarm

volatile float g_seconds_counter = 0;

// Communication functions for SPI
static void 
USART_init(uint16_t ubrr) 
{
	/* Set baud rate in the USART Baud Rate Registers (UBRR) */
	UBRR0H = (unsigned char) (ubrr >> 8);
	UBRR0L = (unsigned char) ubrr;
	
	/* Enable receiver and transmitter on RX0 and TX0 */
	UCSR0B |= (1 << RXEN0) | (1 << TXEN0); //NOTE: the ATmega2560 has 4 UARTs: 0,1,2,3
	
	/* Set frame format: 8 bit data, 2 stop bit */
	UCSR0C |= (1 << USBS0) | (3 << UCSZ00);
}

static void
USART_Transmit(unsigned char data, FILE *stream) 
{
	/* Wait until the transmit buffer is empty*/
	while(!(UCSR0A & (1 << UDRE0))) 
	{
		;
	}
	/* Puts the data into a buffer, then sends/transmits the data */
	UDR0 = data;
}

static char 
USART_Receive(FILE *stream) 
{
	/* Wait until the transmit buffer is empty*/
	while(!(UCSR0A & (1 << UDRE0))) 
	{
		;
	}
	/* Get the received data from the buffer */
	return UDR0;
}

// Setup the stream functions for UART
FILE uart_output = FDEV_SETUP_STREAM(USART_Transmit, NULL, _FDEV_SETUP_WRITE);
FILE uart_input = FDEV_SETUP_STREAM(NULL, USART_Receive, _FDEV_SETUP_READ);


// Timer 3 for buzzer interrupt function
/* timer/counter1 compare match A interrupt vector */
 ISR 
 (TIMER3_COMPA_vect) 
 {
     TCNT3 = 0; // reset timer counter
 }

// Timer 1 interrupt function for timeout
ISR
(TIMER1_COMPA_vect)
{ 
	lcd_clrscr();
	lcd_home();
	lcd_puts("Timeout occurred!");
	_delay_ms(2000);
	
	lcd_clrscr();
	lcd_puts("Enter Password:");
	lcd_gotoxy(0,1);
	
	timeout = 1;
	TCNT1 = 0;
	TIMSK1 &= ~(1 << OCIE1A);
}

// Function to initialize timer
void 
init_timer() 
{
	cli(); //stop interrupts
	
	TCCR1A = 0; // set entire TCCR1A register to 0
	TCCR1B = 0; // same for TCCR1B
	TCNT1  = 0; // initialize counter value to 0
	// set compare match register for 1hz increments
	OCR1A = 65535; // = (16*10^6) / (1*1024) - 1 (must be <65536)
	// turn on CTC mode
	TCCR1B |= (1 << WGM12);
	// Set CS10 and CS12 bits for 1024 prescaler
	TCCR1B |= (1 << CS12) | (1 << CS10);
	// enable timer compare interrupt
	TIMSK1 |= (1 << OCIE1A);
	
	// Enable interrupts command
	sei();
}

// Function to initializing the buzzer
void 
init_buzzer() 
{
	    /* set up the ports and pins */
	    DDRE |= (1 << PE3); // OC3A is located in digital pin 5
		
	    // Enable interrupts command
	    sei();
		
	    TCNT1 = 0;
	    TIMSK1 &= ~(1 << OCIE1A);
		
	    /* set up the 16-bit timer/counter3, mode 9 */
	    TCCR3B = 0; // reset timer/counter 3
	    TCNT3  = 0;
	    TCCR3A |= (1 << 6); // set compare output mode to toggle

	    // mode 9 phase correct
	    TCCR3A |= (1 << 0); // set register A WGM[1:0] bits
	    TCCR3B |= (1 << 4); // set register B WBM[3:2] bits
	    TIMSK3 |= (1 << 1); // enable compare match A interrupt
	    OCR3A = 2443;   // A7 3250 Hz, no prescaler, empirical
}

// Function for the input of current password
int 
inputPassword(char password[8]) 
{
	TCNT1 = 0;
	TIMSK1 &= ~(1 << OCIE1A);
	int outcome = 0;
	
	int i = 0;
	char givenPassword[8] = "";
	uint8_t key;
	lcd_clrscr();
	lcd_puts("Enter Password:");
	lcd_gotoxy(0,1);

	while(i < 8) 
	{
		init_timer();	
		
		key = KEYPAD_GetKey();
		
		// If timeout has previously happened return to "Enter password"
		// section, with no user inputs given
		if (timeout != 0) 
		{
			i = 0;
			timeout = 0;
		}
		// keys 35 = #, 42 = *, 65 = A, 66 = B	REFERENCE: https://www.irongeek.com/alt-numpad-ascii-key-combos-and-chart.html
		if (key != 35 && key != 42 && key != 65 && key != 66) // Password is restricted to only numbers for simplicity
		{ 
			
			// When C is pressed - C = 67
			if (key == 67) 
			{
				if (i != 0) 
				{				
					lcd_gotoxy(i - 1,1);
					lcd_puts(" ");
					i--;
					lcd_gotoxy(i,1);
					
					if (i == 0) 
					{
						lcd_gotoxy(0,1);
					}
				} 
				
			}
			// When D is pressed check if the current password and the given password match
			// D = 68
			else if (key == 68)
			{
				TCNT1 = 0;
				TIMSK1 &= ~(1 << OCIE1A);
				
				// Checks if the length of the password exceeds 4 => Incorrect password
				// Since the maximum length of the password is limited to 4
				if (strlen(givenPassword) >= 5)
				{
					lcd_clrscr();
					lcd_gotoxy(1, 0);
					lcd_puts("Given Password");
					lcd_gotoxy(1, 1);
					lcd_puts("* INCORRECT *");
					_delay_ms(3000);
					outcome = 0;
					break;
				}
				// Checks if the given password matches the current password
				else if (strncmp(password, givenPassword, 4) == 0)
				{
					lcd_clrscr();
					lcd_gotoxy(1, 0);
					lcd_puts("Given Password");
					lcd_gotoxy(1, 1);
					lcd_puts("** CORRECT **");
					TCCR3B = 0; // Turn the buzzer off
					_delay_ms(2000);
					lcd_clrscr();
					outcome = 1;
					break;
				}
				// Check if given password is shorter than 4 
				else
				{
					lcd_clrscr();
					lcd_gotoxy(1, 0);
					lcd_puts("Given Password");
					lcd_gotoxy(1, 1);
					lcd_puts("* INCORRECT *");
					_delay_ms(2000);
					outcome = 0;
					break;
				}
			} 
			// Check if a number key is pressed
			else 
			{
				givenPassword[i] = key;
				lcd_putc(key);
				i++;
				_delay_ms(100);				
			}
		}
	}
	// Returns 1 if successful and 0 if not
	return outcome;
}

// Function to change password - returns new password
const char* 
changePassword(char currentPassword[8]) 
{
	
	int i = 0;
	char givenPassword[8] = "";
	uint8_t key;
	lcd_clrscr();
	lcd_puts("New Password:");
	lcd_gotoxy(0,1);

	// Length of password
	while(i < 8) 
	{
		key = KEYPAD_GetKey();
		
		// keys 35 = #, 42 = *, 65 = A, 66 = B	REFERENCE: https://www.irongeek.com/alt-numpad-ascii-key-combos-and-chart.html
		if (key != 35 && key != 42 && key != 65 && key != 66) // Password is restricted to only numbers for simplicity
		{
			// C for backspace - C = 67
			if (key == 67) 
			{
				if (i != 0) 
				{
					lcd_gotoxy(i - 1,1);
					lcd_puts(" ");
					lcd_gotoxy(i,1);
					i--;
					
					if (i == 0) 
					{
						lcd_gotoxy(0,1);
					}
				}
				
			}
			// If D is pressed - D = 68
			else if (key == 68) 
			{
				if (strlen(givenPassword) == 4) // Check if given password length is 4
				{
					currentPassword = givenPassword;
					break;
				}
				else if (strlen(givenPassword) <= 3) // Check if given password length is smaller than 4
				{
					lcd_clrscr();
					lcd_gotoxy(1,0);
					lcd_puts("** Password **");
					lcd_gotoxy(3,1);
					lcd_puts("Too  Short");
					_delay_ms(2000);
					
					// Loop until a suitable password is given
					while(1)
					{
						strcpy(givenPassword, changePassword(currentPassword));
						if (strlen(givenPassword) == 4)
						{
							currentPassword = givenPassword;
							break;
						}
					}
					break;
				}
				else // Check if given password length is greater than 4
				{
					lcd_clrscr();
					lcd_gotoxy(1,0);
					lcd_puts("** Password **");
					lcd_gotoxy(4,1);
					lcd_puts("Too Long");
					_delay_ms(2000);
					
					// Loop until a suitable password is given
					while(1)
					{
						strcpy(givenPassword, changePassword(currentPassword));
						if (strlen(givenPassword) == 4)
						{
							currentPassword = givenPassword;
							break;
						}
					}
					break;					
				}
			} 
			else // When number key is pressed add it to givenPassword array
			{
				givenPassword[i] = key;
				lcd_putc(key);
				i++;
				_delay_ms(500);
				
				// Check if given passwords length is 8
				// If it is - Reset
				if (strlen(givenPassword) >= 8) // This has been made to avoid bugs
				{
					i = 0;
					strcpy(givenPassword, "");
					lcd_clrscr();
					lcd_puts("Press D to reset");		
				}
			}
		}
	}

	return currentPassword;
}


int 
main() 
{
	// For starters the password is 1234, but it can be changed
	char password[8] = "1234";
	
	// outcome is for checking whether the given password was right or wrong
	int outcome;
	
	// State is set to inactive in the begin
	state = 0;
	
	// Initializing keypad input variable
	uint8_t key;

	// Initializing LCD, Buzzer, and Keypad
	lcd_init(LCD_DISP_ON);
	init_buzzer();
	lcd_clrscr();
	KEYPAD_Init();
	
	// For SPI
	/* set SS, MOSI and SCK as output, pins 53 (PB0), 51 (PB2) and 52 (PB1) */
	DDRB |= (1 << PB0) | (1 << PB1) | (1 << PB2); // SS as output
	/* set SPI enable and master/slave select, making MEGA the master */
	SPCR |= (1 << 6) | (1 << 4);
	/* set SPI clock rate to 1 MHz */
	SPCR |= (1 << 0);

	// initialize the UART with 9600 BAUD
	USART_init(MYUBRR);
	
	// redirect the stdin and stdout to UART functions
	stdout = &uart_output;
	stdin = &uart_input;
	
	char spi_receive_pir[20];
	char spi_send_pir[20];
	
	// Main loop for the program
	while(1)
	{
		switch(state) // State has 4 values - 0 -> inactive, 1 -> Alarm active waiting for movement, 2 -> Change password, 3- > Alarm active
		{
			
			case 0: // Alarm not armed state
			
				lcd_clrscr();
				lcd_gotoxy(1, 0);
				lcd_puts("* ALARM OFF *");
				lcd_gotoxy(1, 1);
				lcd_puts("Press * to arm");
				key = KEYPAD_GetKey();
				
				// If * key is pressed alarms is activated
				if (key == 42) 
				{
					state = 1;
					break;
				// If # key is pressed go to changing password mode
				} 
				else if (key == 35) 
				{
					state = 2;
					break;
				} 
				_delay_ms(100);
			break;
			
			case 1: // Alarm active state
			
				lcd_clrscr();
				lcd_gotoxy(2, 0);
				lcd_puts("* ALARM ON *");
				
				strcpy(spi_receive_pir, "");
				
				
				// Loop for motion detection sensor - communicates with slave
				while (1) 
				{
					  /* send byte to slave and receive a byte from slave */
					  PORTB &= ~(1 << PB0); // SS LOW
					  
					  for(int8_t spi_data_index = 0; spi_data_index < sizeof(spi_send_pir); spi_data_index++) 
					  {
						  
						  SPDR = spi_send_pir[spi_data_index]; // send byte using SPI data register
						  _delay_us(5);
						  while(!(SPSR & (1 << SPIF))) 
						  {
							  /* wait until the transmission is complete */
							  ;
						  }
						  _delay_us(5);
						  spi_receive_pir[spi_data_index] = SPDR; // receive byte from the SPI data register
						  
					  }
					  
					  PORTB |= (1 << PB0); // SS HIGH
					  printf(spi_receive_pir);
					  printf("\n");
					  
					  // If there is movement then the system goes into alarm mode
					  if (strcmp(spi_receive_pir, "movement_detected") == 0) 
					  {
						  
						  state = 3;
						  break;
					  } 
				
				_delay_ms(500);
				}
			break;
				
			case 2: // Change password state
				
				lcd_clrscr();
				lcd_puts("Redirecting to");
				lcd_gotoxy(0, 1);
				lcd_puts("Change Password");
				_delay_ms(2000);
				// First we require old PIN
				outcome = inputPassword(password);
				
				// If given password was incorrect go into the if statement
				if (outcome == 0) 
				{
					state = 3; // Go to alarm state
				} 
				// If given password was correct go into the else statement
				else 
				{
					lcd_clrscr();	
					lcd_puts("A to change");
					lcd_gotoxy(0,1);
					lcd_puts("B to cancel");
					_delay_ms(2000);
					
					// A calls for changePassword function 
					key = KEYPAD_GetKey();

					if (key == 65) // A = 65
					{
						strcpy(password, changePassword(password));	
						lcd_clrscr();
						lcd_puts("Password change:");
						lcd_gotoxy(0, 1);
						lcd_puts("** SUCCESSFUL **");	
						_delay_ms(2000);
						state = 0; // Return to offline mode
						break;
					} 
					else if (key == 66) // B = 66
					{
						state = 0;
						break;
					}
					lcd_clrscr();
				}
			break;
			
			case 3: // Alarm active state
			
				lcd_clrscr();
				lcd_puts("Alarm is active");
				lcd_gotoxy(0,1);
				lcd_puts("Press # - Disarm");			
				
				while(1)
				{
					// Turn buzzer on
					TCCR3B |= (1 << 4); // set register B WBM[3:2] bits
					TCCR3B |= (1 << 0); // set prescaling to 1 (no prescaling)
								
					key = KEYPAD_GetKey();
						
					if (key == 35) // 35 = #
					{
						outcome = inputPassword(password);
						if (outcome == 0) 
						{
							// If password was wrong try again
							while(1)
							{
								outcome = inputPassword(password);
								if (outcome == 1) {
									state = 0;
									break;
								}
							}
							break;
							
						} 
						else 
						{
							// If password was correct disarm alarm
							state = 0;
						}
						break;
					}
				}
			break;
    
			}
	}
	return 0;
}