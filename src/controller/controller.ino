#include <mcp2515.h>
#include <assert.h>

#include "../shared/shared.hpp"

#define STATE_INACTIVE 0
#define STATE_HELLO    1
#define STATE_GAME     2

#define OBUS_GAME_DURATION  60  // Duration of the game in seconds
#define OBUS_MAX_STRIKEOUTS  3  // Number of strikeouts allowed until game over


MCP2515 mcp2515(10);

uint8_t state = STATE_INACTIVE;
struct module connected_modules_ids[OBUS_MAX_MODULES];
uint8_t nr_connected_modules;
uint8_t strikeouts;

// Bit vectors for checking if game is solved or not
uint8_t unsolved_puzzles[32]; // 256 bits

// TIMERS
uint16_t hello_round_start;
uint16_t game_start;
uint16_t last_update;

struct module this_module = (struct module) {
	.type = OBUS_TYPE_CONTROLLER;
	.id = OBUS_CONTROLLER_ID;
};


void setup() {
	Serial.begin(9600);
	mcp2515.reset();
	mcp2515.setBitrate(CAN_50KBPS);
	mcp2515.setNormalMode();

	state = STATE_INACTIVE;
}


uint8_t check_solved() {
	uint8_t solved = 1;
	for (int i; i<32; i++) {
		if (unsolved_puzzles != 0) {
			solved = 0;
			break;
		}
	}
	return solved;
}


void add_module_to_bit_vector(uint8_t module_id) {
	uint8_t byte_index = module_id >> 3;
	uint8_t bit_index = module_id & 0x07;
	unsolved_puzzles[byte_index] |= 0x1 << bit_index;
}


void solve_module_in_bit_vector(uint8_t module_id) {
	uint8_t byte_index = module_id >> 3;
	uint8_t bit_index = module_id & 0x07;
	unsolved_puzzles[byte_index] &= ~(0x1 << bit_index);
}


void send_message(uint8_t* message, uint8_t length) {
	send_message(message, length, false);
}
void send_message(uint8_t* message, uint8_t length, bool priority) {
	struct can_frame send_frame;

	send_frame.can_id = encode_can_id(this_module, priority);
	send_frame.can_dlc = length;

	memcpy(send_frame.data, message, OBUS_MSG_LENGTH);

	mcp2515.sendMessage(&send_frame);
}


void start_hello() {
	state = STATE_HELLO;
	hello_round_start = millis();
	nr_connected_modules = 0;

	// Zero bit vectors
	for (int i; i<32; i++) {
		unsolved_puzzles[i] = 0;
	}

	uint8_t message[OBUS_MSG_LENGTH];
	message[0] = OBUS_MSGTYPE_C_HELLO;

	send_message(message, 1);

	Serial.println("Start of discovery round");
}


void send_ack() {
	uint8_t message[OBUS_MSG_LENGTH];
	message[0] = OBUS_MSGTYPE_C_ACK;

	send_message(message, 1);
}


void receive_hello() {
	struct can_frame receive_frame;
	uint16_t current_time = millis();

	if (mcp2515.readMessage(&receive_frame) == MCP2515::ERROR_OK) {
		if (receive_frame.data[0] ==  OBUS_MSGTYPE_M_HELLO) {
			struct module new_module = decode_can_id(receive_frame.can_id);
			Serial.print("Registered module ");
			Serial.println(full_module_id(new_module));
			connected_modules_ids[nr_connected_modules] = new_module;
			nr_connected_modules++;

			if (new_module.type == OBUS_TYPE_PUZZLE) {
				add_module_to_bit_vector(full_module_id(new_module));
			}

			send_ack();
			Serial.println("ACK");
		}
	} else if (current_time - hello_round_start > OBUS_DISC_DURATION * 1000) {
		Serial.println("End of discovery round");
		initialize_game();
	}
}


void initialize_game() {
	strikeouts = 0;

	uint16_t game_duration_millis = (uint16_t) OBUS_GAME_DURATION * 1000;

	uint8_t message[OBUS_MSG_LENGTH];
	message[0] = OBUS_MSGTYPE_C_GAMESTART;
	message[1] = (uint8_t) ((game_duration_millis & 0xFF000000) >> 0x18);
	message[2] = (uint8_t) ((game_duration_millis & 0x00FF0000) >> 0x10);
	message[3] = (uint8_t) ((game_duration_millis & 0x0000FF00) >> 0x08);
	message[4] = (uint8_t) (game_duration_millis & 0x000000FF);
	message[5] = strikeouts;
	message[6] = OBUS_MAX_STRIKEOUTS;

	send_message(message, 7);

	game_start = millis();
	last_update = game_start;

	state = STATE_GAME;

	Serial.println("Game started");
}


void receive_module_update() {
	struct can_frame receive_frame;

	if (mcp2515.readMessage(&receive_frame) == MCP2515::ERROR_OK) {
		if (receive_frame.data[0] == OBUS_MSGTYPE_M_STRIKE) {
			strikeouts++;
		} else if (receive_frame.data[0] == OBUS_MSGTYPE_M_SOLVED) {
			uint16_t module_id = full_module_id(decode_can_id(receive_frame.can_id));
			solve_module_in_bit_vector(module_id);
		}
	}
}


void send_game_update(uint8_t status, uint16_t timestamp) {
	uint8_t message[OBUS_MSG_LENGTH];
	message[0] = status;
	message[1] = (uint8_t) ((timestamp & 0xFF000000) >> 0x18);
	message[2] = (uint8_t) ((timestamp & 0x00FF0000) >> 0x10);
	message[3] = (uint8_t) ((timestamp & 0x0000FF00) >> 0x08);
	message[4] = (uint8_t) (timestamp & 0x000000FF);
	message[5] = strikeouts;
	message[6] = OBUS_MAX_STRIKEOUTS;

	send_message(message, 7);
}


void game_loop() {
	uint16_t current_time = millis();
	uint16_t game_duration = current_time - game_start;

	receive_module_update();

	if (check_solved()) {
		Serial.println("Game solved");
		send_game_update(OBUS_MSGTYPE_C_SOLVED, game_duration);
		state = STATE_INACTIVE;
		return;
	} else if (game_duration >= (uint16_t) OBUS_GAME_DURATION * 1000) {
		Serial.println("Time's up");
		send_game_update(OBUS_MSGTYPE_C_TIMEOUT, game_duration);
		state = STATE_INACTIVE;
		return;
	} else if (strikeouts >= OBUS_MAX_STRIKEOUTS) {
		Serial.println("Strikeout");
		send_game_update(OBUS_MSGTYPE_C_STRIKEOUT, game_duration);
		state = STATE_INACTIVE;
		return;
	}

	uint16_t elapsed_time = current_time - last_update;
	if (elapsed_time > OBUS_UPDATE_INTERVAL) {
		Serial.print("Sending game update: ");
		Serial.println(game_duration);
		send_game_update(OBUS_MSGTYPE_C_STATE, (uint16_t) OBUS_GAME_DURATION * 1000 - game_duration);
		last_update = current_time;
	}
}


void loop() {
	switch (state) {
		case STATE_INACTIVE:
			start_hello();
			break;

		case STATE_HELLO:
			receive_hello();
			break;

		case STATE_GAME:
			game_loop();
			break;
	}
}
