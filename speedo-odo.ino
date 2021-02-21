#include <EEPROM.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SwitecX25.h>
#include <limits.h>

// standard X25.168 range 315 degrees at 1/3 degree steps
#define STEPS (315*3)
// For motors connected đto digital pins 4,5,6,7
// The X27 168 has built in stops at 0 and max
// Pin out: Poke the needle pin at your nose and orient the text X27 168 so you can read it.
//          Then pin 1 is to the top left (protruding out on the the other side)
//          pin 2 follows counter clock wise, then 3 and 4 which is to the top right.
// Connect 1 -> out 4, 2 -> out 5, 3 -> out 6 and 4 -> out 7
SwitecX25 motor1(STEPS,4,5,6,7);


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     3 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Vortec box has 4k pulses/mile -> 2485 pulses / km ->
// 1 km/h = 0.69 Hz = 1448 mS, 100 km/h = 69 Hz = 14.4 mS,  200 km/h = 138 Hz = 7.24 mS 
//
// The signal is 0 - +12V square wave signal. It needs a serial resistor at 10Kohm to
// connect it to the Arduino input pin. The input pin has a built in clamping
// zener at 5V, and a 10Kohm in series will make for the allowed 1 mA clamping current.

#define PULSES_PER_KM 2485
int speed_input_pin = 2;

// Pull this pin to GND to reset the odo. 
int odo_reset_pin = 8;

bool toggle = false;
unsigned long savedDistanceAtStart;
volatile unsigned long distanceCount = 0, period[4], lastMicros = 0, currentMicros;

// Settings - can be set via serial commands
int zeromark;  // steps from 0 to where the physical zero mark really is
int maxmark;   // steps to the maxmark
int maxspeed;  // what is the speed at the maxmark

void retrieve_setting_from_eeprom();

void IRQcounter() {
  bool in = digitalRead(2);
  if (in == LOW) {
      digitalWrite(9, toggle);
      toggle = !toggle;
      return;
  }
  currentMicros = micros();
  if (currentMicros - lastMicros < 1000) return; /* reject to short pulses */
  distanceCount++;
  /* Save period in ring buffer so we can weed out spurious pulses  */
  /* period[distanceCount & 3] = currentMicros - lastMicros; */
  period[0] = currentMicros - lastMicros;
  lastMicros = currentMicros;
}


void setup() {
    Serial.begin(9600);

    pinMode(odo_reset_pin, INPUT_PULLUP);
    pinMode(speed_input_pin, INPUT);
    pinMode(9, OUTPUT);
    
    attachInterrupt(digitalPinToInterrupt(speed_input_pin), IRQcounter, RISING);

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Don't proceed, loop forever
    }

    init_rotation_possitions();
    savedDistanceAtStart = get_saved_distance();
    speedo_init();
    retrieve_setting_from_eeprom();
    speedo(0, false);
}

unsigned long longest(unsigned long *periodp) {
    unsigned int max = 0;
    int i;
    for ( i = 0; i < 4; i ++) {
        if ( periodp[i] > max ) {
            max = periodp[i];
        }
    }
    return max;
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
    if (Serial.available())  {
	char c = Serial.read(); 
	if (c == '\r') {  
	    Serial.read(); //gets rid of the following \r
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

void loop() {
    static int mode = RUN_MODE;
    static int modevalue = 0;
    int speed = 0, lastSpeed = 0;
    unsigned long t, startTime = 0, lastDistanceCount;

    unsigned long distance;
    startTime = millis();
    
    while (1) {
	int i = get_command();
	if (i > 0) {
	    if (strcmp(cmd[0], "moveto") == 0) {
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
		speed = (3600000000/PULSES_PER_KM)/period[0]; // period in uS -> speed in km/h
	    }
	    /* Serial.print("period: "); */
	    /* Serial.println(period[0]); */
	
	    odo(distance);
	    speedo(speed, false);
	    motor1.update();
	    save_distance(distance);
	    if (odoResetButton()) {
		Serial.println("RESET");
		reset_distance();
	    }
	} else if ( mode == MOVETO_MODE ) {
	    moveto(modevalue);
	} else if ( mode == SPEEDTEST_MODE ) {
	    speedo(modevalue, false);
	}
    }
}


bool odoResetButton() {
    static int prevState = HIGH;
    bool retval = false;
    int state = digitalRead(odo_reset_pin);
    if(state == LOW && prevState != state) {
	delay(100);
	retval = true;
    }
    prevState = state;
    return retval;
}


unsigned long get_saved_long(int pos) {
    int i;
    unsigned long value;
    byte *bp;
    bp = (byte*)&value;
    
    for( i =0; i<4; i++) {
	bp[i] = EEPROM.read(pos*4 + i);
    }
    return value;
}


void save_long(int pos, unsigned long value) {
    int i;
    byte *bp;
    bp = (byte*)&value;
    for( i =0; i<4; i++) {
	byte rvalue;
	rvalue = EEPROM.read(pos*4 + i);
	if(rvalue != bp[i]) {
	    EEPROM.write(pos*4 + i, bp[i]);
	}
    }
    Serial.print(" saving ");
    Serial.println(value);
    Serial.print(" to pos: ");
    Serial.println(pos);
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
    display.clearDisplay();
    display.setCursor(20, 8);
    display.setTextSize(3);             // Draw 2X-scale text
    display.setTextColor(SSD1306_WHITE);
    display.println(str);
    display.display();
}

#define ZEROPOS 30
void speedo_init() {
    // run the motor against the stops
    motor1.zero();
    // start moving towards the center of the range
    motor1.setPosition(ZEROPOS);
}

#define STEPS_PER_BIG_KPH_MARK 63
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
    motor1.setPosition(nextPos);
    motor1.updateBlocking();
  }
}

void moveto(int pos)
{
    motor1.setPosition(pos);
    motor1.updateBlocking();
}

