#include "buggyradio.h"

#include <Arduino.h>

#include <SPI.h>
#include <RH_RF69.h>

enum PacketType : uint32_t {
    GPS_X_Y,
    STEER_ANGLE;
}

struct Packet {
    PacketType tag;
    union {
        struct { float x; float y; float time; } gps_x_y;
        struct { float degrees; } steer_angle;
    }
}

/************ Radio Setup ***************/

// Change to 434.0 or other frequency, must match RX's freq!
#define RF69_FREQ 434.0

RH_RF69 rf69(RFM69_CS, RFM69_INT);

// put function declarations here:
int myFunction(int, int);

void radio_init() {
	pinMode(RFM69_RST, OUTPUT);
	digitalWrite(RFM69_RST, LOW);

	digitalWrite(RFM69_RST, HIGH);
	delay(10);
	digitalWrite(RFM69_RST, LOW);
	delay(10);

	if (!rf69.init()) {
		while (1) {
			Serial.println("RFM69 radio init failed");
			delay(100);
		}
	}

    // Defaults after init are 434.0MHz, modulation GFSK_Rb250Fd250, +13dbM (for low power module)
    // No encryption
    if (!rf69.setFrequency(RF69_FREQ)) {
        while (1) {
            Serial.println("setFrequency failed");
            delay(100);
        }
    }

    if (!rf69.setModemConfig(RH_RF69::GFSK_Rb250Fd250)) {
        while (1) {
            Serial.println("setModem failed");
            delay(100);
        }
    }

    // If you are using a high power RF69 eg RFM69HW, you *must* set a Tx power with the
    // ishighpowermodule flag set like this:
    rf69.setTxPower(20, true);  // range from 14-20 for power, 2nd arg must be true for 69HCW

	Serial.println("RFM69 radio init OK!");
}

void radio_transmit(const uint8_t *data, size_t size) {
	rf69.waitPacketSent();
	rf69.send(data, size);
}

void radio_send_gps(double x, double y, uint64_t gps_time) {
    Packet p{};
    p.tag = GPS_X_Y;
    p.gps_x_y.x = x;
    p.gps_x_y.y = y;
    p.gps_x_y.time = gps_time;
    radio_transmit(&p, sizeof(p));
}

void radio_send_steering(double angle) {
    Packet p{};
    p.tag = STEER_ANGLE;
    p.steer_angle.degrees = angle;
    radio_transmit(&p, sizeof(p));
}

bool radio_receive(uint8_t *data, size_t size) {
	return rf69.recv(data, size);
}

bool radio_available() {
	return rf69.available();
}