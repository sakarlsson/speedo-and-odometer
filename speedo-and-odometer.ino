#include <EEPROM.h>
#include <SPI.h>
#include <Wire.h>
#include <limits.h>
#include "x27-stepper.h"

#define ODO

#ifdef ODO

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET_PIN     3 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN);

#endif

// Vortec box has 4k pulses/mile -> 2485 pulses / km ->
// 1 km/h = 0.69 Hz = 1448 mS, 100 km/h = 69 Hz = 14.4 mS,  200 km/h = 138 Hz = 7.24 mS 
//
// The signal is 0 - +12V square wave signal. It needs a serial resistor at 10Kohm to
// connect it to the Arduino input pin. The input pin has a built in clamping
// zener at 5V, and a 10Kohm in series will make for the allowed 1 mA clamping current.

#define PULSES_PER_KM 2485
#define SPEED_INPUT_PIN 2

// Pull this pin to GND to reset the odo. 
#define ODO_RESET_PIN 3

// Motor driver pins
#define X12_RESET 4
#define X12_DIR_A 7
#define X12_STEP_A 8
X27Stepper motor1(X12_STEP_A, X12_DIR_A, X12_RESET);


#define IGN_SENSE_PIN 9 
#define BAT_CUT_PIN 10 

unsigned long savedDistanceAtStart;
volatile unsigned long distanceCount = 0, period, lastMicros = 0, currentMicros;

// Settings - can be set via serial commands
int zeromark;  // steps from 0 to where the physical zero mark really is
int maxmark;   // steps to the maxmark
int maxspeed;  // what is the speed at the maxmark

void retrieve_setting_from_eeprom();

unsigned long period_buf[2];
void IRQcounter() {
  bool in = digitalRead(SPEED_INPUT_PIN);
  if (in == LOW) {
		return;
  }
  currentMicros = micros();
  // if (currentMicros - lastMicros < 1000) return; reject to short pulses 
  distanceCount++;
  // Save period in shift buffer so we can weed out spurious pulses
	period_buf[1] = period_buf[0];
  period_buf[0] = currentMicros - lastMicros;

	// reject to large jumps by waiting for two consecutive large diffs
	if ( abs(period_buf[0] - period_buf[1]) < 900 ) { // uS
		// reject too small changes to aviod flicker 
		if ( abs(period - period_buf[0]) >= 6 ) {  // uS 
			period  = period_buf[0]; 
		}
	}
  lastMicros = currentMicros;
}


void setup() {
	Serial.begin(9600);

#ifdef ODO
	pinMode(ODO_RESET_PIN, INPUT_PULLUP);
#endif
	pinMode(IGN_SENSE_PIN, INPUT);
	pinMode(SPEED_INPUT_PIN, INPUT);

	pinMode(BAT_CUT_PIN, OUTPUT);
	digitalWrite(BAT_CUT_PIN, HIGH); // Make NPN pull down FET gate to ground to keep bat

	attachInterrupt(digitalPinToInterrupt(SPEED_INPUT_PIN), IRQcounter, RISING);

	// SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
#ifdef ODO    
	if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
		Serial.println(F("SSD1306 allocation failed"));
		for (;;); // Don't proceed, loop forever
	}
#endif

	init_rotation_possitions();
	savedDistanceAtStart = get_saved_distance();
	speedo_init();
	retrieve_setting_from_eeprom();
	speedo(0, false);
}

// tokenize(s) split s into global array cmd 
// return number of tokens
#define MAX_ARGSZ 15
#define MAX_ARGS 4
char cmd[MAX_ARGS][MAX_ARGSZ+1];
int tokenize(char *s) {
	int i = 0;
	char *p;
	if(strlen(s) == 0) {
		return 0;
	}
	strncpy(cmd[i], strtok(s, " \t"), MAX_ARGSZ);
	cmd[i][MAX_ARGSZ] = '\0';
	i++;
	while ((p = strtok(NULL, " \t")) != NULL && i < MAX_ARGS) {
		strncpy(cmd[i], p, MAX_ARGSZ);
		cmd[i][MAX_ARGSZ] = '\0';
		i++;
	}
	return i;
}
// get_command() reads from serial until a line is complete
// do not block
// returns tokenize(line) or 0 if not yet a full line 
int get_command(){
	static char line[MAX_ARGS * (MAX_ARGSZ + 1) + 1];
	static int lineix = 0;
	char buf[8];
	if (Serial.available())  {
		char c = Serial.read(); 
		sprintf(buf, "X %02x", c);
		/* Serial.println(buf);			 */
		if (c == '\r' || c == '\n') {  
      Serial.read(); //gets rid of the following \r or \n
      line[lineix] = '\0';
      lineix = 0;
      return tokenize(line);
		} else {     
      line[lineix] = c; 
      lineix++;
		}
	}
	return 0;
}

#define RUN_MODE 0
#define MOVETO_MODE 1
#define SPEEDTEST_MODE 2
#define SHUTDOWN_MODE 3

void loop() {
	int p;
	static int mode = RUN_MODE;
	static int modevalue = 0;
	int speed = 0, lastSpeed = 0;
	unsigned long t, startTime = 0, lastDistanceCount;

	unsigned long distance;
	startTime = millis();
    
	while (1) {
		int i = get_command();
		if (i > 0) {
      if (strcmp(cmd[0], "zero") == 0) {
				speedo_init();
      } else if (strcmp(cmd[0], "moveto") == 0) {
				mode = MOVETO_MODE;
				modevalue = atoi(cmd[1]);
				Serial.println("MOVETO_MODE");
      } else if (strcmp(cmd[0], "speed") == 0) {
				mode = SPEEDTEST_MODE;
				modevalue = atoi(cmd[1]);
				speedo(modevalue, true);
				Serial.println("SPEEDTEST_MODE");
      } else if (strcmp(cmd[0], "set") == 0) {
				if (strcmp(cmd[1], "zeromark") == 0) {
					zeromark = atoi(cmd[2]); 
					save_zeromark_to_eeprom();
				} else if (strcmp(cmd[1], "maxmark") == 0) {
					maxmark = atoi(cmd[2]); 
					save_maxmark_to_eeprom();
				} else if (strcmp(cmd[1], "maxspeed") == 0) {
					maxspeed = atoi(cmd[2]); 
					save_maxspeed_to_eeprom();
				}
				Serial.println("OK");
      } else if (strcmp(cmd[0], "status") == 0) {
				retrieve_setting_from_eeprom();
      } else if (strcmp(cmd[0], "run") == 0) {
				mode = RUN_MODE;
				Serial.println("RUN_MODE");
      }
		}
		if ( mode == RUN_MODE ) {
      t = millis();
      distance = savedDistanceAtStart + (distanceCount)/PULSES_PER_KM; /* in cm */
      if(lastDistanceCount != distanceCount) {
				lastDistanceCount = distanceCount;
				startTime = t;
      }
      /* In case we have no pulses within a sec, we assume speed = 0 */
      if (t - startTime > 1000) {
				speed = 0;
      } else {
				speed = (3600000000/PULSES_PER_KM)/period; // period in uS -> speed in km/h
      }

			/* if( p != period ) { */
			/* 	Serial.print("p: "); */
			/* 	Serial.println(period); */
			/* 	p = period; */
			/* } */
			if( p != speed ) {
				Serial.print("s: ");
				Serial.print(speed);
				Serial.print("  p: ");
				Serial.println(period);
				p = speed;
			}
  
#ifdef ODO
      odo(distance);
#endif
      speedo(speed, false);
      motor1.update();
      save_distance(distance);
      if (odoResetButton()) {
				Serial.println("RESET");
				reset_distance();
      }
			bool ign = digitalRead(IGN_SENSE_PIN);
			if ( ign == LOW ) {
				mode = SHUTDOWN_MODE;
				Serial.println("SHUTDOWN_MODE");
			}
		} else if ( mode == MOVETO_MODE ) {
      moveto(modevalue);
		} else if ( mode == SPEEDTEST_MODE ) {
      speedo(modevalue, false);
		} else if ( mode == SHUTDOWN_MODE ) {
			speedo(0,false);
			digitalWrite(BAT_CUT_PIN, LOW); // Turn off BAT and try to die
			delay(1000);
			// In case BAT comes back before we die. 
			while(digitalRead(IGN_SENSE_PIN) == LOW) {
							delay(1000);
			}
			digitalWrite(BAT_CUT_PIN, HIGH); // Turn on BAT again
			Serial.println("IGN came back - RUN_MODE");
			mode = RUN_MODE;
		}
	}
}


bool odoResetButton() {
#ifdef ODO 
	static int prevState = HIGH;
	bool retval = false;
	int state = digitalRead(ODO_RESET_PIN);
	if(state == LOW && prevState != state) {
		delay(100);
		retval = true;
	}
	prevState = state;
	return retval;
#else
	return false;
#endif
}


unsigned long get_saved_long(int pos) {
	int i;
	unsigned long value;
	byte *bp;
	bp = (byte*)&value;
	for(i=0; i<4; i++) {
		bp[i] = EEPROM.read(pos*4 + i);
	}
	return value;
}

/* Write a long (4 bytes) to pos  */
void save_long(int pos, unsigned long value) {
	int i;
	byte *bp;
	bp = (byte*)&value;
	for( i =0; i<4; i++) {
		byte rvalue;
		rvalue = EEPROM.read(pos*4 + i);
		/* Only write if it has changed - to save writes */
		if(rvalue != bp[i]) {
	    EEPROM.write(pos*4 + i, bp[i]);
		}
	}
}

/* We will write to multiple possitions i eeprom to get more cycle life */
#define ROTATE_POSITIONS 8
	
#define MAX_KM 1000000
void init_rotation_possitions() {
	/* set possition to zero if unreasonable big */
	int i;
	unsigned long value;
	for (i=0; i<ROTATE_POSITIONS; i++) {
		value = get_saved_long(i);
		if (value >= MAX_KM) {
			save_long(i, (unsigned long)0);
		}
	}
}

void reset_distance() {
	int i;
	for (i=0; i<ROTATE_POSITIONS; i++) {
		save_long(i, 0);
	}
	savedDistanceAtStart = 0;
	distanceCount = 0;
}

unsigned long get_saved_distance() {
	int i;
	unsigned long value, value_max = 0;
	for (i=0; i<ROTATE_POSITIONS; i++) {
		value = get_saved_long(i);
		if ( value > value_max) {
	    value_max = value;
		}
	}
	return value_max;
}

void save_distance(unsigned long distance) {
	int i, savepos = 0;
	unsigned long value, value_min = 1000000;
	value = get_saved_distance();
	if( value >= distance) return;

	for (i=0; i<ROTATE_POSITIONS; i++) {
		value = get_saved_long(i);
		if ( value < value_min) {
	    value_min = value;
	    savepos = i;
		}
	}
	save_long(savepos, distance);
}

void retrieve_setting_from_eeprom() {
	/* They are stored after ROTATE_POSITIONS */
	zeromark = (int)(get_saved_long(ROTATE_POSITIONS));
	maxmark = (int)(get_saved_long(ROTATE_POSITIONS+1));
	if ( maxmark == 0 ) {
		maxmark = 720;
	}
	maxspeed = (int)(get_saved_long(ROTATE_POSITIONS+2));
	if ( maxspeed == 0 ) {
		maxspeed = 200;
	}
	Serial.print("Restoring from eeprom  zeromark: ");
	Serial.print(zeromark);
	Serial.print(" maxmark: ");
	Serial.print(maxmark);
	Serial.print(" maxspeed: ");
	Serial.println(maxspeed);
    
}

void save_zeromark_to_eeprom() {
	save_long(ROTATE_POSITIONS, (unsigned long)zeromark);
}

void save_maxmark_to_eeprom() {
	save_long(ROTATE_POSITIONS+1, (unsigned long)maxmark);
}

void save_maxspeed_to_eeprom() {
	save_long(ROTATE_POSITIONS+2, (unsigned long)maxspeed);
}

void odo(long i) {
	char str[10];
	sprintf(str, "%6d", i);
#ifdef ODO
	display.clearDisplay();
	display.setCursor(20, 8);
	display.setTextSize(3);             // Draw 2X-scale text
	display.setTextColor(SSD1306_WHITE);
	display.println(str);
	display.display();
#endif
}

#define ZEROPOS 30
void speedo_init() {
	// run the motor against the stops
	motor1.zero();
	// start moving towards the center of the range
	motor1.setPos(ZEROPOS);
}

// speedin in km/h 
void speedo(int speedin, bool debug)
{
  static int nextPos = 0;
  static int currentPos = 0;
  unsigned long speed = speedin;

  nextPos = ((((speed << 10) / maxspeed) * (maxmark - zeromark)) >> 10) + zeromark;
  if (debug) {
		Serial.print(" x: ");
		Serial.print((speed << 10) / maxspeed);
		Serial.print(" zm: ");
		Serial.print(zeromark);
		Serial.print(" mm: ");
		Serial.print(maxmark);
		Serial.print(" ms: ");
		Serial.print(maxspeed);
		Serial.print(" sp: ");
		Serial.println(speedin);
		Serial.print("nextPos: ");
		Serial.println(nextPos);
  }
  if( abs(nextPos - currentPos) > 2) {
    currentPos = nextPos;
    motor1.setPos(nextPos);
    while(motor1.isMoving()) {
			motor1.update();
    }
  }
}

void moveto(int pos)
{
	if ( motor1.getCurrentPos() == pos ){
		return;
	}
	motor1.setPos(pos);

	while(motor1.isMoving()) {
		motor1.update();
	}
}
