#include "events.h"

#include <cstdio>
#include <cstring>

#include "test_support.h"

using namespace kbcore;

namespace {

Event make_event(uint32_t id, const char *type, uint32_t created_ms, std::string data_json) {
  Event e;
  e.id = id;
  e.type = type;
  e.created_ms = created_ms;
  e.data_json = std::move(data_json);
  return e;
}

TEST(pour_data_minimal_has_only_required_fields) {
  PourData d;
  d.meter = 1;
  d.pour_id = "5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415";
  d.volume_ml = 355.0f;
  d.duration_ms = 7100;
  const std::string json = pour_data_json(d);

  CHECK_EQ(json, std::string(R"({"meter":1,"pour_id":"5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415",)"
                             R"("volume_ml":355.000,"duration_ms":7100})"));
}

TEST(pour_data_full_includes_optionals) {
  PourData d;
  d.meter = 0;
  d.pour_id = "5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415";
  d.volume_ml = 355.2f;
  d.duration_ms = 7100;
  d.user = "mikey";
  d.auth_device = "core.rfid";
  d.auth_token = "0089f2c4";
  d.ticks = 1919;
  d.ml_per_tick = 0.185f;
  d.tick_series = "0:3 100:14";
  const std::string json = pour_data_json(d);

  CHECK(json.find("\"pour_id\":\"5f8e2c34") != std::string::npos);
  CHECK(json.find("\"user\":\"mikey\"") != std::string::npos);
  CHECK(json.find("\"ticks\":1919") != std::string::npos);
  CHECK(json.find("\"ml_per_tick\":0.1850") != std::string::npos);
  CHECK(json.find("\"tick_series\":\"0:3 100:14\"") != std::string::npos);
}

TEST(pour_data_zero_ticks_is_still_emitted) {
  // ticks == 0 is a legitimate diagnostic value (volume overridden); only the
  // UINT32_MAX sentinel omits the field.
  PourData d;
  d.meter = 0;
  d.pour_id = "x";
  d.volume_ml = 1.0f;
  d.duration_ms = 1;
  d.ticks = 0;
  CHECK(pour_data_json(d).find("\"ticks\":0") != std::string::npos);
}

TEST(token_data_status_absent_means_server_decides) {
  const std::string ask = token_data_json("core.rfid", "0089f2c4", true, TokenStatus::NONE, "");
  CHECK_EQ(ask.find("\"status\""), std::string::npos);

  const std::string local = token_data_json("core.rfid", "0089f2c4", true, TokenStatus::ACCEPTED, "mikey");
  CHECK(local.find("\"status\":\"accepted\"") != std::string::npos);
  CHECK(local.find("\"user\":\"mikey\"") != std::string::npos);
}

TEST(status_data_includes_config_always) {
  StatusData d;
  d.boot = true;
  d.fw_version = "4.0.0";
  d.uptime_ms = 1234;
  d.events_dropped = 0;
  d.heartbeat_ms = 60000;
  d.pour_update_ms = 1000;
  d.queue_capacity = 16;
  const std::string json = status_data_json(d);

  CHECK(json.find("\"state\":\"boot\"") != std::string::npos);
  CHECK(json.find("\"config\":{\"heartbeat_ms\":60000,\"pour_update_ms\":1000,\"queue_capacity\":16}") !=
        std::string::npos);
  // No meters were provided; the optional array is absent.
  CHECK_EQ(json.find("\"meters\""), std::string::npos);
}

TEST(batch_age_is_computed_from_send_time) {
  Event e = make_event(17, "pour", 1000, R"({"meter":0,"pour_id":"x","volume_ml":1.000,"duration_ms":1})");
  const std::string json = serialize_batch("kegboard-a1b2c3", "9f3a2c1b", 5000, {&e});

  CHECK(json.find("\"age_ms\":4000") != std::string::npos);
  CHECK(json.find("\"sent_uptime_ms\":5000") != std::string::npos);

  // The same event serialized later reports a larger age: age is a property
  // of the send, not the event.
  const std::string later = serialize_batch("kegboard-a1b2c3", "9f3a2c1b", 9000, {&e});
  CHECK(later.find("\"age_ms\":8000") != std::string::npos);
}

TEST(batch_age_survives_millis_rollover) {
  Event e = make_event(1, "pour", 0xFFFFFF00u, R"({"meter":0,"pour_id":"x","volume_ml":1.000,"duration_ms":1})");
  const std::string json = serialize_batch("d", "b", 500, {&e});
  // 0x100 + 500 = 756 ms elapsed across the wrap.
  CHECK(json.find("\"age_ms\":756") != std::string::npos);
}

TEST(batch_omits_time_when_clock_never_synced) {
  Event e = make_event(1, "temperature", 0, R"({"sensor":"t","temp_c":4.000})");
  CHECK_EQ(serialize_batch("d", "b", 10, {&e}).find("\"time\""), std::string::npos);

  e.time = "2026-08-03T18:02:11Z";
  CHECK(serialize_batch("d", "b", 10, {&e}).find("\"time\":\"2026-08-03T18:02:11Z\"") != std::string::npos);
}

TEST(format_boot_id_is_8_hex) {
  CHECK_EQ(format_boot_id(0x9f3a2c1bu), std::string("9f3a2c1b"));
  CHECK_EQ(format_boot_id(0x1u), std::string("00000001"));
}

TEST(format_uuid4_sets_version_and_variant) {
  uint8_t bytes[16] = {0};
  const std::string id = format_uuid4(bytes);
  CHECK_EQ(id.size(), static_cast<size_t>(36));
  CHECK_EQ(id[14], '4');                   // version nibble
  CHECK(id[19] == '8' || id[19] == '9' ||  // variant nibble: 10xx
        id[19] == 'a' || id[19] == 'b');
  CHECK_EQ(id[8], '-');
  CHECK_EQ(id[13], '-');
  CHECK_EQ(id[18], '-');
  CHECK_EQ(id[23], '-');
}

TEST(format_uuid4_differs_with_input) {
  uint8_t a[16] = {0}, b[16] = {0};
  b[15] = 1;
  CHECK(format_uuid4(a) != format_uuid4(b));
}

}  // namespace

// With an argument, emit one sample document per event type into that
// directory and exit; script/check-events-schema.py validates them against
// the normative schemas. This is what ties the firmware's actual output to
// the protocol document.
static int emit_samples(const char *dir) {
  using std::string;

  PourData full;
  full.meter = 0;
  full.pour_id = "5f8e2c34-9d1b-4a7e-b02c-8f13d9a6e415";
  full.volume_ml = 355.2f;
  full.duration_ms = 7100;
  full.user = "mikey";
  full.auth_device = "core.rfid";
  full.auth_token = "0089f2c4";
  full.ticks = 1919;
  full.ml_per_tick = 0.185f;
  full.tick_series = "0:3 100:14 200:31";

  PourData minimal;
  minimal.meter = 3;
  minimal.pour_id = "0d4f9b82-6e3a-4c15-a7b8-2c9d0e1f6a3b";
  minimal.volume_ml = 10.5f;
  minimal.duration_ms = 900;

  StatusData st;
  st.boot = false;
  st.fw_version = "4.0.0";
  st.uptime_ms = 7523000;
  st.has_rssi = true;
  st.rssi_dbm = -61;
  st.events_dropped = 2;
  st.heartbeat_ms = 60000;
  st.pour_update_ms = 1000;
  st.queue_capacity = 16;
  st.meters.push_back({0, 920791, 0.185f});
  st.meters.push_back({1, 42031, 0.185f});

  struct Sample {
    const char *name;
    Event event;
  };
  Sample samples[] = {
      {"pour-full", {}},   {"pour-minimal", {}},   {"pour-update", {}}, {"temperature", {}},    {"token-ask", {}},
      {"token-local", {}}, {"token-detached", {}}, {"status", {}},      {"command-result", {}},
  };
  samples[0].event = Event{17, "pour", 1000, "2026-08-03T18:02:11Z", pour_data_json(full)};
  samples[1].event = Event{18, "pour", 1500, "", pour_data_json(minimal)};
  samples[2].event = Event{19, "pour_update", 2000, "", pour_update_data_json(0, full.pour_id, 120.4f, 2400)};
  samples[3].event = Event{20, "temperature", 2500, "", temperature_data_json("thermo-28ff641d8fbb0517", 4.25f)};
  samples[4].event =
      Event{21, "token", 3000, "", token_data_json("core.rfid", "0089f2c4", true, TokenStatus::NONE, "")};
  samples[5].event = Event{22, "token", 3100, "",
                           token_data_json("onewire", "0000000012345678", true, TokenStatus::ACCEPTED, "mikey")};
  samples[6].event =
      Event{23, "token", 3200, "", token_data_json("onewire", "0000000012345678", false, TokenStatus::NONE, "")};
  samples[7].event = Event{24, "status", 3300, "", status_data_json(st)};
  samples[8].event = Event{25, "command_result", 3400, "", command_result_data_json("cmd_8f21", "ok", "")};

  // One batch per sample, plus one combined batch exercising multiple events.
  std::vector<const Event *> all;
  for (auto &s : samples) {
    const std::string batch = serialize_batch("kegboard-a1b2c3", "9f3a2c1b", 10000, {&s.event});
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.json", dir, s.name);
    FILE *f = fopen(path, "w");
    if (f == nullptr) {
      fprintf(stderr, "cannot write %s\n", path);
      return 1;
    }
    fputs(batch.c_str(), f);
    fclose(f);
    all.push_back(&s.event);
  }

  const std::string combined = serialize_batch("kegboard-a1b2c3", "9f3a2c1b", 10000, all);
  char path[512];
  snprintf(path, sizeof(path), "%s/combined.json", dir);
  FILE *f = fopen(path, "w");
  if (f == nullptr)
    return 1;
  fputs(combined.c_str(), f);
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1)
    return emit_samples(argv[1]);

  printf("%s\n", "events");
  RUN(pour_data_minimal_has_only_required_fields);
  RUN(pour_data_full_includes_optionals);
  RUN(pour_data_zero_ticks_is_still_emitted);
  RUN(token_data_status_absent_means_server_decides);
  RUN(status_data_includes_config_always);
  RUN(batch_age_is_computed_from_send_time);
  RUN(batch_age_survives_millis_rollover);
  RUN(batch_omits_time_when_clock_never_synced);
  RUN(format_boot_id_is_8_hex);
  RUN(format_uuid4_sets_version_and_variant);
  RUN(format_uuid4_differs_with_input);
  printf("%s: %d checks, %d failures\n\n", "events", ::kbtest::checks(), ::kbtest::failures());
  return ::kbtest::failures() == 0 ? 0 : 1;
}
