#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"
#include "metadata_hub.h"
#include "mqtt.h"
#include <mosquitto.h>

// this holds the mosquitto client
struct mosquitto *global_mosq = NULL;
int connected = 0;

// mosquitto logging
void _cb_log(__attribute__((unused)) struct mosquitto *mosq, __attribute__((unused)) void *userdata,
             int level, const char *str) {
  switch (level) {
  case MOSQ_LOG_DEBUG:
    debug(3, "%s", str);
    break;
  case MOSQ_LOG_INFO:
    debug(3, "%s", str);
    break;
  case MOSQ_LOG_NOTICE:
    debug(3, "%s", str);
    break;
  case MOSQ_LOG_WARNING:
    inform("%s", str);
    break;
  case MOSQ_LOG_ERR: {
    die("MQTT: Error: %s\n", str);
  }
  }
}

// mosquitto message handler
void on_message(__attribute__((unused)) struct mosquitto *mosq,
                __attribute__((unused)) void *userdata, const struct mosquitto_message *msg) {

  // null-terminate the payload
  char payload[msg->payloadlen + 1];
  memcpy(payload, msg->payload, msg->payloadlen);
  payload[msg->payloadlen] = 0;

  debug(2, "[MQTT]: received Message on topic %s: %s\n", msg->topic, payload);

  // All recognized commands
  char *commands[] = {"command",    "beginff",  "beginrew",   "mutetoggle",
                      "nextitem",   "previtem", "pause",      "playpause",
                      "play",       "stop",     "playresume", "shuffle_songs",
                      "volumedown", "volumeup", "disconnect", NULL};

  int it = 0;

  // send command if it's a valid one
  while (commands[it] != NULL) {
    if ((size_t)msg->payloadlen >= strlen(commands[it]) &&
        strncmp(msg->payload, commands[it], strlen(commands[it])) == 0) {
      debug(2, "[MQTT]: Received Recognized Command: %s\n", commands[it]);
      if (strcmp(commands[it], "disconnect") == 0) {
        debug(2, "[MQTT]: Disconnect Command: %s\n", commands[it]);
        release_play_lock(NULL); // stop any current session and don't replace it
      } else {
        debug(2, "[MQTT]: DACP Command: %s\n", commands[it]);
        send_simple_dacp_command(commands[it]);
      }
      break;
    }
    it++;
  }
}

void on_disconnect(__attribute__((unused)) struct mosquitto *mosq,
                   __attribute__((unused)) void *userdata, __attribute__((unused)) int rc) {
  connected = 0;
  debug(2, "[MQTT]: disconnected");
}

void on_connect(struct mosquitto *mosq, __attribute__((unused)) void *userdata,
                __attribute__((unused)) int rc) {
  connected = 1;
  debug(2, "[MQTT]: connected");

  // subscribe if requested
  if (config.mqtt_enable_remote) {
    char remotetopic[strlen(config.mqtt_topic) + 8];
    snprintf(remotetopic, strlen(config.mqtt_topic) + 8, "%s/remote", config.mqtt_topic);
    mosquitto_subscribe(mosq, NULL, remotetopic, 0);
  }

  // send autodiscovery messages if enabled
  if (config.mqtt_enable_autodiscovery && config.mqtt_publish_parsed) {
    send_autodiscovery_messages(mosq);
  }
}

// function to send autodiscovery messages for Home Assistant
void send_autodiscovery_messages(struct mosquitto *mosq) {
  const char *device_name = config.service_name;
#ifdef CONFIG_AIRPLAY_2
  const char *device_id = config.airplay_device_id ? config.airplay_device_id : config.service_name;
#else
  const char *device_id = config.service_name;
#endif
  const char *device_id_no_colons = str_replace(device_id, ":", "");
  const char *sw_version = get_version_string();
  const char *model = "shairport-sync";
  const char *model_friendly = "Shairport Sync";
  const char *manufacturer = "Mike Brady";
  const char *autodiscovery_prefix = (config.mqtt_autodiscovery_prefix != NULL)
                                         ? config.mqtt_autodiscovery_prefix
                                         : "homeassistant";

  char topic[512];
  char payload[1280];
  char device_payload[512];
  char id_string[128];

  snprintf(device_payload, sizeof(device_payload),
           "\"device\": {"
           "\"identifiers\": [\"%s\"],"
           "\"name\": \"%s\","
           "\"model\": \"%s\","
           "\"sw_version\": \"%s\","
           "\"manufacturer\": \"%s\""
           "}",
           device_id, device_name, model_friendly, sw_version, manufacturer);

  // when adding sensors here, be sure to also update sensor_names and icons below!
  const char *sensors[] = {"artist",
                           "album",
                           "title",
                           "genre",
                           "format",
                           "output_format",
                           "output_frame_rate",
                           "track_id",
                           "client_ip",
                           "client_mac_address",
                           "client_name",
                           "client_model",
                           "client_device_id",
                           "server_ip",
                           "volume",
                           "active",
                           "playing",
                           NULL};

  const char *sensor_names[] = {"Artist",
                                "Album",
                                "Title",
                                "Genre",
                                "Format",
                                "Output Format",
                                "Output Frame Rate",
                                "Track ID",
                                "Client IP",
                                "Client MAC Address",
                                "Client Name",
                                "Client Model",
                                "Client Device ID",
                                "Server IP",
                                "Volume",
                                "Active Session",
                                "Playing"};

  const char *icons[] = {
      "mdi:account-music",            // artist
      "mdi:album",                    // album
      "mdi:music",                    // title
      "mdi:music-box-multiple",       // genre
      "mdi:file",                     // format
      "mdi:file",                     // output format
      "mdi:file-chart",               // output frame rate
      "mdi:identifier",               // track ID
      "mdi:ip",                       // client IP
      "mdi:hexadecimal",              // client MAC address
      "mdi:cellphone-text",           // client name
      "mdi:cellphone-text",           // client model
      "mdi:hexadecimal",              // client device ID
      "mdi:ip-network",               // server IP
      "mdi:volume-high",              // volume
      "mdi:play-box-multiple",        // active
      "mdi:play-box-multiple-outline" // playing
  };

  for (int i = 0; sensors[i] != NULL; i++) {
    bool is_binary_sensor =
        (strcmp(sensors[i], "active") == 0 || strcmp(sensors[i], "playing") == 0);
    bool is_volume_sensor = strcmp(sensors[i], "volume") == 0;

    snprintf(topic, sizeof(topic), "%s/%ssensor/%s_%s/%s/config", autodiscovery_prefix,
             is_binary_sensor ? "binary_" : "", model, device_id_no_colons, sensors[i]);

    snprintf(id_string, sizeof(id_string), "%s_%s_%s", model, device_name, sensors[i]);

    snprintf(
        payload, sizeof(payload),
        "{"
        "\"name\": \"%s\","
        "\"state_topic\": \"%s/%s\","
        "\"icon\": \"%s\","
        "\"unique_id\": \"%s\","
        "\"object_id\": \"%s\","
        "%s%s%s"
        "}",
        sensor_names[i], config.mqtt_topic, sensors[i], icons[i], id_string, id_string,
        is_binary_sensor ? "\"payload_on\": \"1\",\"payload_off\": \"0\"," : "",
        is_volume_sensor
            ? "\"value_template\": \"{{ ((value | regex_findall_index("
              "find='^(.+?),', index=0, ignorecase=False) | float / 30 + 1) * 100) | round(0) }}\","
              "\"unit_of_measurement\": \"%\","
            : "",
        device_payload);

    mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, true);
    debug(2, "[MQTT]: published autodiscovery for %s", id_string);
  }
}

// helper function to publish under a topic and automatically append the main topic
void mqtt_publish(char *topic, char *data_in, uint32_t length_in) {
  char *data = data_in;
  uint32_t length = length_in;

  if ((length == 0) && (config.mqtt_empty_payload_substitute != NULL)) {
    length = strlen(config.mqtt_empty_payload_substitute);
    data = config.mqtt_empty_payload_substitute;
  }

  char fulltopic[strlen(config.mqtt_topic) + strlen(topic) + 3];
  snprintf(fulltopic, strlen(config.mqtt_topic) + strlen(topic) + 2, "%s/%s", config.mqtt_topic,
           topic);
  debug(2, "[MQTT]: publishing under %s", fulltopic);

  int rc;
  if ((rc = mosquitto_publish(global_mosq, NULL, fulltopic, length, data, 0,
                              config.mqtt_publish_retain)) !=
      MOSQ_ERR_SUCCESS) {
    switch (rc) {
    case MOSQ_ERR_NO_CONN:
      debug(1, "[MQTT]: Publish failed: not connected to broker");
      break;
    default:
      debug(1, "[MQTT]: Publish failed: unknown error");
      break;
    }
  }
}

// handler for incoming metadata
void mqtt_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length) {
  if (global_mosq == NULL || connected != 1) {
    debug(3, "[MQTT]: Client not connected, skipping metadata handling");
    return;
  }
  if (config.mqtt_publish_raw) {
    uint32_t val;
    char topic[] = "____/____";

    val = htonl(type);
    memcpy(topic, &val, 4);
    val = htonl(code);
    memcpy(topic + 5, &val, 4);
    mqtt_publish(topic, data, length);
  }
  if (config.mqtt_publish_parsed) {
    if (type == 'core') {
      int32_t r;
      uint64_t trackid;
      char trackidstring[32];

      switch (code) {
      case 'asar':
        mqtt_publish("artist", data, length);
        break;
      case 'asal':
        mqtt_publish("album", data, length);
        break;
      case 'asfm':
        mqtt_publish("format", data, length);
        break;
      case 'asgn':
        mqtt_publish("genre", data, length);
        break;
      case 'minm':
        mqtt_publish("title", data, length);
        break;
      case 'mper':
        trackid = *(uint64_t *)(data);
        r = snprintf(trackidstring, sizeof(trackidstring), "%" PRIX64 "", trackid);
        mqtt_publish("track_id", trackidstring, r);
      }
    } else if (type == 'ssnc') {
      switch (code) {
      case 'abeg':
        mqtt_publish("active", "1", 1);
        mqtt_publish("active_start", data, length);
        break;
      case 'acre':
        mqtt_publish("active_remote_id", data, length);
        break;
      case 'aend':
        mqtt_publish("active", "0", 1);
        mqtt_publish("active_end", data, length);
        break;
      case 'asal':
        mqtt_publish("songalbum", data, length);
        break;
      case 'asdk':
        mqtt_publish("songdatakind", data,
                     length); // 0 seem to be a timed item, 1 an untimed stream
        break;
      case 'clip':
        mqtt_publish("client_ip", data, length);
        break;
      case 'cdid':
        mqtt_publish("client_device_id", data, length);
        break;
      case 'cmac':
        mqtt_publish("client_mac_address", data, length);
        break;
      case 'cmod':
        mqtt_publish("client_model", data, length);
        break;
      case 'daid':
        mqtt_publish("dacp_id", data, length);
        break;
      case 'phbt':
        mqtt_publish("frame_position_and_time", data, length);
        break;
      case 'phb0':
        mqtt_publish("first_frame_position_and_time", data, length);
        break;
      case 'ofmt':
        mqtt_publish("output_format", data, length);
        break;
      case 'ofps':
        mqtt_publish("output_frame_rate", data, length);
        break;
      case 'pbeg':
        mqtt_publish("playing", "1", 1);
        mqtt_publish("play_start", data, length);
        break;
      case 'pend':
        mqtt_publish("playing", "0", 1);
        mqtt_publish("play_end", data, length);
        break;
      case 'pfls':
        mqtt_publish("play_flush", data, length);
        break;
      case 'PICT':
        if (config.mqtt_publish_cover) {
          mqtt_publish("cover", data, length);
        }
        break;
      case 'prsm':
        mqtt_publish("playing", "1", 1);
        mqtt_publish("play_resume", data, length);
        break;
      case 'pvol':
        mqtt_publish("volume", data, length);
        break;
      case 'snam':
        mqtt_publish("client_name", data, length);
        break;
      case 'styp':
        mqtt_publish("stream_type", data, length);
        break;
      case 'svip':
        mqtt_publish("server_ip", data, length);
        break;
      case 'svna':
        mqtt_publish("service_name", data, length);
        break;
      }
    }
  }

  return;
}

int initialise_mqtt() {
  debug(1, "Initialising MQTT");
  if (config.mqtt_hostname == NULL) {
    debug(1, "[MQTT]: Not initialized, as the hostname is not set");
    return 0;
  }
  int keepalive = 60;
  mosquitto_lib_init();
  if (!(global_mosq = mosquitto_new(config.service_name, true, NULL))) {
    die("[MQTT]: FATAL: Could not create mosquitto object!");
  }

  if (config.mqtt_cafile != NULL || config.mqtt_capath != NULL || config.mqtt_certfile != NULL ||
      config.mqtt_keyfile != NULL) {
    if (mosquitto_tls_set(global_mosq, config.mqtt_cafile, config.mqtt_capath, config.mqtt_certfile,
                          config.mqtt_keyfile, NULL) != MOSQ_ERR_SUCCESS) {
      die("[MQTT]: TLS Setup failed");
    }
  }

  if (config.mqtt_username != NULL || config.mqtt_password != NULL) {
    if (mosquitto_username_pw_set(global_mosq, config.mqtt_username, config.mqtt_password) !=
        MOSQ_ERR_SUCCESS) {
      die("[MQTT]: Username/Password set failed");
    }
  }
  mosquitto_log_callback_set(global_mosq, _cb_log);

  if (config.mqtt_enable_remote) {
    mosquitto_message_callback_set(global_mosq, on_message);
  }

  mosquitto_disconnect_callback_set(global_mosq, on_disconnect);
  mosquitto_connect_callback_set(global_mosq, on_connect);
  if (mosquitto_connect(global_mosq, config.mqtt_hostname, config.mqtt_port, keepalive)) {
    inform("[MQTT]: Could not establish a mqtt connection");
  }
  if (mosquitto_loop_start(global_mosq) != MOSQ_ERR_SUCCESS) {
    inform("[MQTT]: Could start MQTT Main loop");
  }

  return 0;
}
