#ifndef __MQTT_H__
#define __MQTT_H__

// includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "CC3100.h"

// Control packet types
#define CONNECT        1
#define CONNACK        2
#define PUBLISH        3
#define PUBACK         4
#define PUBREC         5
#define PUBREL         6
#define PUBCOMP        7
#define SUBSCRIBE      8
#define SUBACK         9
#define UNSUBSCRIBE   10
#define UNSUBACK      11
#define PINGREQ       12
#define PINGRESP      13
#define DISCONNECT    14

// Utility struct for representation of a fixed-length byte array
struct ByteArray {
	unsigned int length;
	uint8_t* bytes;
};
typedef struct ByteArray ByteArray;

struct Response {
	unsigned int type;
	unsigned int status;
};
typedef struct Response Response;

// exported functions
void mqtt_connect(char* hostname, unsigned short port, char* clientID, char* username, char* password);
void mqtt_publish(char* topic, char* message);
uint8_t mqtt_isConnected();
void mqtt_disconnect();
void mqtt_ping();

#endif
