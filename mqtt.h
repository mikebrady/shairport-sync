#ifndef MQTT_H
#define MQTT_H
#include <mosquitto.h>
#include <stdint.h>

int initialise_mqtt();
void mqtt_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length);
void mqtt_publish(char *topic, char *data, uint32_t length);
void mqtt_setup();
void send_autodiscovery_messages(struct mosquitto *mosq);
void on_connect(struct mosquitto *mosq, void *userdata, int rc);
void on_disconnect(struct mosquitto *mosq, void *userdata, int rc);
void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg);
void _cb_log(struct mosquitto *mosq, void *userdata, int level, const char *str);

void metadata_mqtt_queue_init();
void metadata_mqtt_queue_stop();
int send_metadata_to_mqtt_queue(const uint32_t type, const uint32_t code,
                           const char *data, const uint32_t length, rtsp_message *carrier,
                           int block);
#endif /* #ifndef MQTT_H */
