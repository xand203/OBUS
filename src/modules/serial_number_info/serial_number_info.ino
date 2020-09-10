// (c) 2020, redfast00
// See the LICENSE file for conditions for copying

#include <obus_module.h>
#include <LiquidCrystal.h>

#define SERIAL_NUMBER_SIZE 7

uint8_t serial_number[SERIAL_NUMBER_SIZE];

// true when we need to update the LCD display
bool render_now = false;
LiquidCrystal lcd(7, 6, 5, 4, 3, 2);


void setup() {
	Serial.begin(115200);
	lcd.begin(16, 2);
	lcd.clear();
	obus_module::setup(OBUS_TYPE_INFO, OBUS_INFO_ID_DEVELOPMENT);
}

obus_can::message message;

void loop() {
	obus_module::loopInfo(&message, info_generator);
	if (render_now) {
		render_now = false;
		lcd.home();
		lcd.clear();
		lcd.print("Serial Number");
		lcd.setCursor(0, 1);
		for (int i = 0; i < SERIAL_NUMBER_SIZE; i++) {
			lcd.write(serial_number[i]);
		}
	}
}

int info_generator(uint8_t* buffer) {
	for (int i = 0; i < SERIAL_NUMBER_SIZE; i++) {
		uint8_t generated = random(26 + 10);
		if (generated < 26)
			serial_number[i] = 'A' + generated;
    else
		  serial_number[i] = '0' + (generated - 26);
	}
	memcpy(buffer, serial_number, SERIAL_NUMBER_SIZE);
	render_now = true;
	return SERIAL_NUMBER_SIZE;
}
