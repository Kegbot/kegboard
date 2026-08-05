#include "events.h"

#include <cstdio>

#include "json_writer.h"

namespace kbcore {

std::string pour_data_json(const PourData &d) {
  JsonWriter w;
  w.begin_object();
  w.key("meter_number");
  w.value(static_cast<uint32_t>(d.meter));
  w.key("pour_id");
  w.value(d.pour_id);
  w.key("volume_ml");
  w.value(d.volume_ml, 3);
  w.key("duration_ms");
  w.value(d.duration_ms);
  if (!d.auth_device.empty()) {
    w.key("auth_device");
    w.value(d.auth_device);
  }
  if (!d.auth_token.empty()) {
    w.key("auth_token");
    w.value(d.auth_token);
  }
  if (!d.grant_id.empty()) {
    w.key("grant_id");
    w.value(d.grant_id);
  }
  if (d.ticks != UINT32_MAX) {
    w.key("ticks");
    w.value(d.ticks);
  }
  if (d.ml_per_tick > 0.0f) {
    w.key("ml_per_tick");
    w.value(d.ml_per_tick, 4);
  }
  if (!d.tick_series.empty()) {
    w.key("tick_series");
    w.value(d.tick_series);
  }
  w.end_object();
  return w.str();
}

std::string pour_update_data_json(uint8_t meter, const std::string &pour_id, float volume_ml, uint32_t duration_ms) {
  JsonWriter w;
  w.begin_object();
  w.key("meter_number");
  w.value(static_cast<uint32_t>(meter));
  w.key("pour_id");
  w.value(pour_id);
  w.key("volume_ml");
  w.value(volume_ml, 3);
  w.key("duration_ms");
  w.value(duration_ms);
  w.end_object();
  return w.str();
}

std::string temperature_data_json(const std::string &sensor, float temp_c) {
  JsonWriter w;
  w.begin_object();
  w.key("sensor");
  w.value(sensor);
  w.key("temp_c");
  w.value(temp_c, 3);
  w.end_object();
  return w.str();
}

std::string token_data_json(const std::string &auth_device, const std::string &token, bool attached) {
  JsonWriter w;
  w.begin_object();
  w.key("auth_device");
  w.value(auth_device);
  w.key("token");
  w.value(token);
  w.key("action");
  w.value(attached ? "attached" : "detached");
  w.end_object();
  return w.str();
}

std::string status_data_json(const StatusData &d) {
  JsonWriter w;
  w.begin_object();
  w.key("state");
  w.value(d.boot ? "boot" : "heartbeat");
  w.key("fw_version");
  w.value(d.fw_version);
  w.key("uptime_ms");
  w.value(d.uptime_ms);
  if (d.has_rssi) {
    w.key("wifi_rssi_dbm");
    w.value(d.rssi_dbm);
  }
  w.key("events_dropped");
  w.value(d.events_dropped);
  w.key("config");
  w.begin_object();
  w.key("heartbeat_ms");
  w.value(d.heartbeat_ms);
  w.key("pour_update_ms");
  w.value(d.pour_update_ms);
  w.key("queue_capacity");
  w.value(d.queue_capacity);
  w.end_object();
  if (!d.meters.empty()) {
    w.key("meters");
    w.begin_array();
    for (const auto &m : d.meters) {
      w.begin_object();
      w.key("meter_number");
      w.value(static_cast<uint32_t>(m.meter));
      w.key("total_ticks");
      w.value(m.total_ticks);
      w.key("ml_per_tick");
      w.value(m.ml_per_tick, 4);
      w.end_object();
    }
    w.end_array();
  }
  if (!d.relays.empty()) {
    w.key("relays");
    w.begin_array();
    for (uint8_t relay : d.relays) {
      w.begin_object();
      w.key("relay_number");
      w.value(static_cast<uint32_t>(relay));
      w.end_object();
    }
    w.end_array();
  }
  w.end_object();
  return w.str();
}

std::string grant_end_data_json(const GrantEnd &end) {
  JsonWriter w;
  w.begin_object();
  w.key("meter_numbers");
  w.begin_array();
  for (uint8_t meter : end.meters)
    w.value(static_cast<uint32_t>(meter));
  w.end_array();
  w.key("reason");
  w.value(grant_end_reason_str(end.reason));
  if (!end.auth_device.empty()) {
    w.key("auth_device");
    w.value(end.auth_device);
  }
  if (!end.token.empty()) {
    w.key("auth_token");
    w.value(end.token);
  }
  w.key("grant_id");
  w.value(end.grant_id);
  w.key("volume_ml");
  w.value(end.volume_ml, 3);
  w.key("duration_ms");
  w.value(end.duration_ms);
  w.end_object();
  return w.str();
}

std::string command_result_data_json(const std::string &command, const char *result, const std::string &message) {
  JsonWriter w;
  w.begin_object();
  w.key("command");
  w.value(command);
  w.key("result");
  w.value(result);
  if (!message.empty()) {
    w.key("message");
    w.value(message);
  }
  w.end_object();
  return w.str();
}

std::string serialize_batch(const std::string &device, const std::string &boot_id, uint32_t now_ms,
                            const std::vector<const Event *> &events) {
  JsonWriter w;
  w.begin_object();
  w.key("v");
  w.value(static_cast<uint32_t>(1));
  w.key("device");
  w.value(device);
  w.key("boot_id");
  w.value(boot_id);
  w.key("sent_uptime_ms");
  w.value(now_ms);
  w.key("events");
  w.begin_array();
  for (const Event *e : events) {
    w.begin_object();
    w.key("id");
    w.value(e->id);
    w.key("type");
    w.value(e->type);
    w.key("age_ms");
    w.value(now_ms - e->created_ms);
    if (!e->time.empty()) {
      w.key("time");
      w.value(e->time);
    }
    w.key("data");
    w.raw_value(e->data_json);
    w.end_object();
  }
  w.end_array();
  w.end_object();
  return w.str();
}

std::string format_boot_id(uint32_t random) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08x", random);
  return std::string(buf);
}

std::string format_uuid4(const uint8_t random_bytes[16]) {
  uint8_t b[16];
  for (int i = 0; i < 16; i++)
    b[i] = random_bytes[i];
  b[6] = (b[6] & 0x0f) | 0x40;  // version 4
  b[8] = (b[8] & 0x3f) | 0x80;  // RFC 4122 variant

  char buf[37];
  snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2],
           b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return std::string(buf);
}

}  // namespace kbcore
