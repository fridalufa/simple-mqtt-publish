#include "mqtt.h"

// This is a complete mockup file!
int main(){

	// the host is test.mosquitto.org 
	mqtt_connect("85.119.83.194", 1883, "myClientID", NULL, NULL);

	while(!mqtt_isConnected()){}

	mqtt_publish("temp/random", "15.0");

	mqtt_ping();

	sleep(1);

	mqtt_disconnect();

	return 0;
}
