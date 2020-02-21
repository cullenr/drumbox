#include "Arduino.h"

#define FS 8000 // Speech engine sample rate

void setup() {
	// Auto-setup.
	// 
	// Enable the speech system whenever say() is called.

	pinMode(3,OUTPUT);
	// Timer 2 set up as a 62500Hz PWM.
	//
	// The PWM 'buzz' is well above human hearing range and is
	// very easy to filter out.
	//
	TCCR2A = _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
	TCCR2B = _BV(CS20);
	TIMSK2 = 0;

	// Unfortunately we can't calculate the next sample every PWM cycle
	// as the routine is too slow. So use Timer 1 to trigger that.

	// Timer 1 set up as a 8000Hz sample interrupt
	TCCR1A = 0;
	TCCR1B = _BV(WGM12) | _BV(CS10);
	TCNT1 = 0;
	OCR1A = F_CPU / FS;
	TIMSK1 = _BV(OCIE1A);
}

ISR(TIMER1_COMPA_vect) {
	static int i = 0;
	i++;

	if(i < 32) 
		OCR2B = 0;
	else if (i < 64)
		OCR2B = 255;
	else
		i = 0;
}

void loop() {
}
