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
SwitecX25 motor1(STEPS,4,5,6,7);


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     3 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

volatile unsigned long distanceCount = 0, period[4], lastMicros = 0, currentMicros;

unsigned long savedDistanceAtStart;

int speed_input_pin = 2;
int odo_reset_pin = 8;
bool toggle = false;

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


#define PULSES_PER_KM 2485

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
    speedo(0);
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

void loop() {
    byte value;
    int speed = 0, lastSpeed = 0;
    unsigned long t, startTime = 0, lastDistanceCount;
    

    unsigned long distance;
    startTime = millis();
    
    while (1) {
	byte *bp;
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
	    speed = (36000000000/PULSES_PER_KM)/period[0];
	}
	/* Serial.print("period: "); */
	/* Serial.println(period[0]); */
	
	odo(distance);
	speedo(speed);
	motor1.update();
	save_distance(distance);
	if (odoResetButton()) {
	    Serial.println("RESET");
	    reset_distance();
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
void speedo(int speedin)
{
  static int nextPos = 0;
  static int currentPos = 0;
  unsigned long speed = speedin;

  if (speed <= 1000) {
      nextPos = (int) (speed * STEPS_PER_BIG_KPH_MARK / 200);
  } else if (speed <= 1200) {
      nextPos = (int) (5 * STEPS_PER_BIG_KPH_MARK + (speed - 1000) * STEPS_PER_BIG_KPH_MARK / 100);
  } else {
      nextPos = (int) (7 * STEPS_PER_BIG_KPH_MARK + (speed - 1200) * STEPS_PER_BIG_KPH_MARK / 200);
  }
  nextPos += ZEROPOS;

  if( abs(nextPos - currentPos) > 2) {
    currentPos = nextPos;
    motor1.setPosition(nextPos);
    motor1.updateBlocking();
  }
}

