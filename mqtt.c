#include "mqtt.h"

/******************************************************************************
 *                              Function declarations                         *
 ******************************************************************************/

ByteArray encodeLength(int length);
int decodeLength(uint8_t bytes[]);
ByteArray encodePayload(char* payload);
void sendPacket(unsigned int msgType, ByteArray* variableHeader, ByteArray* payload);
ByteArray encodeString (char* str);
void handleResponses(char* bytes, size_t length);


/******************************************************************************
 *                              Global variables                              *
 ******************************************************************************/
uint8_t connected = 0;
uint8_t mqttReady = 0;

unsigned long sock;

uint8_t* sendBuffer;
uint16_t sendBufferLength;

/******************************************************************************
 *                             Utility functions                              *
 ******************************************************************************/

// helper functions to decode and encode lengths
ByteArray encodeLength(int length){

	// MQTT allows a maximum of four bytes for length encoding
	static uint8_t encodedLength[4];
	int bytesUsed = 0;

	do {
		uint8_t encodedByte = length % 128;
		length = length / 128;
		if (length > 0){
			encodedByte = encodedByte | 128;
		}
		encodedLength[bytesUsed++] = encodedByte;
	} while (length > 0);


	return (ByteArray) {
		.length = bytesUsed,
		.bytes = encodedLength
	};
}

int decodeLength(uint8_t bytes[]){

	unsigned int multiplier = 1;
	unsigned int value = 0;
	unsigned int pos = 0;

	uint8_t encodedByte = 0;
	do {
		encodedByte = bytes[pos++];
		value += (encodedByte & 127) * multiplier;
		multiplier *= 128;
		if (multiplier > 128*128*128 && (encodedByte & 128) != 0){
			return -1;
		}
	} while ((encodedByte & 128) != 0);

	return value;
}

// encodes a string with its length
ByteArray encodeString(char* str){

	int strLength = strlen(str);
	int encodedStrLength = 2 + strLength;
	uint8_t* encodedStr = (uint8_t*) malloc(encodedStrLength);

	// encodes a string length according to the MQTT protocol in two bytes
	encodedStr[0] = (strLength & 0xFF00) >> 8; // MSB
	encodedStr[1] = (strLength & 0xFF);        // LSB

	memcpy(encodedStr + 2, str, strlen(str));

	return (ByteArray) {
		.length = encodedStrLength,
		.bytes = encodedStr
	};
}

/******************************************************************************
 *                         Connection related functions                       *
 ******************************************************************************/

long tcp_write_callback(char* tx, uint16_t tx_len, uint16_t socket_Position) {

	long txLength = sendBufferLength;
    if (sendBufferLength > 0) {

        memcpy(tx, sendBuffer, sendBufferLength);
        free(sendBuffer);
        sendBufferLength = 0;
        sendBuffer = NULL;

    }

    return txLength;
}

void tcp_read_callback(char* rx, uint16_t rx_len, sockaddr* from, uint16_t socket_Position) {
	handleResponses(rx, rx_len);
}

void tcp_exception_callback(uint16_t pos){
	// TODO: TCP Exception handling
}


void mqtt_connect(char* host, uint16_t port, char* clientID, char* username, char* password){

  
	//unsigned long server_ip = 0L;
	//gethostbyname(host, strlen(host), &server_ip, 4);

  // The host parameter is currently ignored and hardcoded into the openSocket function
	// Open socket
	sock = CC3100_openSocket(
		127,
		0,
		0,
		1,
		port,
		TCP_Client,
	    tcp_read_callback,
	    tcp_write_callback,
	    tcp_exception_callback
	);

    connected = 1;

    // set connect flags
    uint8_t connectFlags = 0x00;

    connectFlags = connectFlags | (username != NULL) << 7;
    connectFlags = connectFlags | (password != NULL) << 6;

    // create variable connect header
	static uint8_t connectVariableHeaderBytes[] = {
		0x00, // protocol name length MSB
		0x04, // protocol name length LSB
		'M',  // |
		'Q',  // | protocol name
		'T',  // |
		'T',  // |
		0x04, // MQTT Protocol Level (0x04 = MQTT 3.1.1)
		0x00, // connect flags (replaced by actual flags later)
		0x01, // keep alive MSB
		0x2C  // keep alive LSB
	};

	// replace connect flags placeholder in the header
	connectVariableHeaderBytes[7] = connectFlags;

	ByteArray variableHeader = (ByteArray) {
		.length = 10,
		.bytes = connectVariableHeaderBytes
	};


	// encode clientID and - if set - username and password
	ByteArray encodedClientID = encodeString(clientID);
	int payloadLength = encodedClientID.length;

	ByteArray encodedUsername = {0};
	if(username != NULL){
		encodedUsername = encodeString(username);
		payloadLength +=  encodedUsername.length;
	}

	ByteArray encodedPassword = {0};
	if(password != NULL){
		encodedPassword = encodeString(password);
		payloadLength += encodedPassword.length;
	}

	// merge encoded clientID, username and password into a single payload byte array
	uint8_t payloadBytes[payloadLength];

	memcpy(payloadBytes, encodedClientID.bytes, encodedClientID.length);
	free(encodedClientID.bytes);

	if(encodedUsername.bytes != NULL){
		memcpy(payloadBytes+encodedClientID.length, encodedUsername.bytes, encodedUsername.length);
		//free(encodedUsername.bytes);
	}

	if(encodedPassword.bytes != NULL){
		uint8_t* start = payloadBytes + encodedClientID.length + (payloadLength - encodedClientID.length - encodedPassword.length);
		memcpy(start, encodedPassword.bytes, encodedPassword.length);
		//free(encodedPassword.bytes);
	}

	ByteArray payload = (ByteArray) {
		.length = payloadLength,
		.bytes = payloadBytes
	};

    sendPacket(CONNECT, &variableHeader, &payload);
}

// evaluates to true when everything is ready and messages can be published
uint8_t mqtt_isConnected(){
	return (connected && mqttReady);
}

// clean disconnect
void mqtt_disconnect(){
	sendPacket(DISCONNECT, NULL, NULL);
	connected = 0;
	mqttReady = 0;
	CC3100_closeSocket(sock);
}

/******************************************************************************
 *                              Sending and receiving                         *
 ******************************************************************************/


void sendPacket(unsigned int msgType, ByteArray* variableHeader, ByteArray* payload){

	if (!connected || (!mqttReady && msgType != CONNECT)){
		// TODO: SendPacket error handling
		return;
	}

	// calculate total remaining length of the package
	int remainingLength = 0;

	if(variableHeader != NULL){
		remainingLength += variableHeader->length;
	}

	if(payload != NULL){
		remainingLength += payload->length;
	}

	ByteArray encodedLength = encodeLength(remainingLength);

	// create the fixed header
	uint8_t fixedHeaderBytes[encodedLength.length + 1];

	fixedHeaderBytes[0] = msgType << 4;
	memcpy(fixedHeaderBytes+1, encodedLength.bytes, encodedLength.length);

	ByteArray fixedHeader = (ByteArray) {
		.length = encodedLength.length + 1,
		.bytes = fixedHeaderBytes
	};

	// craft packet from parts
	int packetSize = fixedHeader.length + remainingLength;
	uint8_t* packet = (uint8_t*) malloc(packetSize);

	memcpy(packet, fixedHeader.bytes, fixedHeader.length);

	int variableHeaderLength = 0;
	if(variableHeader != NULL){
		variableHeaderLength = variableHeader->length;
		memcpy(packet + fixedHeader.length, variableHeader->bytes, variableHeader->length);
	}

	if(payload != NULL){
		memcpy(packet + fixedHeader.length + variableHeaderLength, payload->bytes, payload->length);
	}

	// wait until last packet has been sent
	while(sendBufferLength > 0){}

	sendBuffer = packet;
	sendBufferLength = packetSize;
}

// handles only CONNACK and PINGRESP packets
void handleResponses(char* bytes, size_t length){

	// all valid MQTT packets are at least two bytes long
	if (length < 2){
		return;
	}

	uint8_t msgType = bytes[0] >> 4;

	if(msgType == CONNACK){
		if(bytes[3] == 0x00){
			mqttReady = 1;
			// TODO: Init PING sending via timer
		}else{
			// TODO: Error handling
		}
	}

	if(msgType == PINGRESP){
		// TODO: ping handling
	}
}

/******************************************************************************
 *                                   Publish                                  *
 ******************************************************************************/

// publish function
void mqtt_publish(char* topic, char* message){

	ByteArray variableHeader = encodeString(topic);

	ByteArray encodedMessage = (ByteArray) {
		.length = strlen(message),
		.bytes = (uint8_t*) message
	};

	sendPacket(PUBLISH, &variableHeader, &encodedMessage);
	free(variableHeader.bytes);
}


/******************************************************************************
 *                                   Ping                                     *
 ******************************************************************************/


void mqtt_ping(){
	sendPacket(PINGREQ, NULL, NULL);
}
