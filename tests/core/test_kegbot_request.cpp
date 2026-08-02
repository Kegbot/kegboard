#include "kegbot_request.h"

#include "test_support.h"

using kegboard::DrinkReport;
using kegboard::HttpCall;
using kegboard::KegbotRequestBuilder;
using kegboard::ThermoReport;

namespace {

bool body_has(const std::string &body, const std::string &field) {
  if (body == field)
    return true;
  if (body.rfind(field + "&", 0) == 0)
    return true;
  return body.find("&" + field + "&") != std::string::npos ||
         (body.size() >= field.size() + 1 &&
          body.compare(body.size() - field.size() - 1, field.size() + 1, "&" + field) == 0);
}

KegbotRequestBuilder make_builder() {
  KegbotRequestBuilder b;
  b.set_base_url("https://kegbot.example.com");
  return b;
}

DrinkReport make_drink() {
  DrinkReport d;
  d.meter_name = "kegboard-A1B2.flow0";
  d.ticks = 500;
  d.volume_ml = 355.0f;
  d.duration_s = 7;
  d.pour_time_unix = 1700000000;
  d.pour_uptime_s = 3600;
  return d;
}

TEST(base_url_trailing_slash_is_normalized) {
  KegbotRequestBuilder b;
  b.set_base_url("https://kegbot.example.com/");
  CHECK_EQ(b.base_url(), std::string("https://kegbot.example.com"));

  b.set_base_url("https://kegbot.example.com///");
  CHECK_EQ(b.base_url(), std::string("https://kegbot.example.com"));
}

TEST(base_url_ending_in_api_is_normalized) {
  // Users pasting the API root instead of the site root should still work.
  KegbotRequestBuilder b;
  b.set_base_url("https://kegbot.example.com/api");
  CHECK_EQ(b.base_url(), std::string("https://kegbot.example.com"));

  b.set_base_url("https://kegbot.example.com/api/");
  CHECK_EQ(b.base_url(), std::string("https://kegbot.example.com"));
}

TEST(drink_post_targets_tap_endpoint) {
  HttpCall call = make_builder().drink_post(make_drink(), 1700000007, 3607);

  CHECK_EQ(call.method, std::string("POST"));
  CHECK_EQ(call.url, std::string("https://kegbot.example.com/api/taps/kegboard-A1B2.flow0"));
}

TEST(drink_post_includes_ticks_and_duration) {
  HttpCall call = make_builder().drink_post(make_drink(), 1700000007, 3607);

  CHECK(body_has(call.body, "ticks=500"));
  CHECK(body_has(call.body, "duration=7"));
}

TEST(drink_post_omits_volume_by_default) {
  // Kegbot Server holds per-meter calibration and exposes a calibration UI.
  // Sending volume_ml too would create a second source of truth.
  HttpCall call = make_builder().drink_post(make_drink(), 1700000007, 3607);
  CHECK_EQ(call.body.find("volume_ml"), std::string::npos);
}

TEST(drink_post_includes_volume_when_enabled) {
  KegbotRequestBuilder b = make_builder();
  b.set_send_volume(true);
  HttpCall call = b.drink_post(make_drink(), 1700000007, 3607);

  CHECK(body_has(call.body, "volume_ml=355.000"));
}

TEST(drink_post_uses_wall_clock_when_synced) {
  HttpCall call = make_builder().drink_post(make_drink(), 1700000007, 3607);

  CHECK(body_has(call.body, "pour_time=1700000000"));
  CHECK(body_has(call.body, "now=1700000007"));
}

TEST(drink_post_falls_back_to_uptime_when_clock_never_synced) {
  // The server only uses (now - pour_time), so a monotonic pair is just as
  // accurate as a wall-clock pair — and is all an unsynced device has.
  DrinkReport d = make_drink();
  d.pour_time_unix = 0;

  HttpCall call = make_builder().drink_post(d, 0, 3607);

  CHECK(body_has(call.body, "pour_time=3600"));
  CHECK(body_has(call.body, "now=3607"));
}

TEST(drink_post_falls_back_when_pour_predates_sync) {
  // Pour stamped before NTP came up, delivered after: the wall-clock pair
  // would imply a pour in 1970, so the uptime pair must win.
  DrinkReport d = make_drink();
  d.pour_time_unix = 0;

  HttpCall call = make_builder().drink_post(d, 1700000007, 3607);

  CHECK(body_has(call.body, "pour_time=3600"));
  CHECK(body_has(call.body, "now=3607"));
}

TEST(queued_pour_reports_correct_elapsed_time) {
  // A pour delivered an hour late must still say it happened an hour ago.
  DrinkReport d = make_drink();
  HttpCall call = make_builder().drink_post(d, 1700003600, 7200);

  CHECK(body_has(call.body, "pour_time=1700000000"));
  CHECK(body_has(call.body, "now=1700003600"));
}

TEST(drink_post_omits_empty_username_and_series) {
  HttpCall call = make_builder().drink_post(make_drink(), 1700000007, 3607);

  CHECK_EQ(call.body.find("username"), std::string::npos);
  CHECK_EQ(call.body.find("tick_time_series"), std::string::npos);
}

TEST(drink_post_includes_username_and_series_when_present) {
  DrinkReport d = make_drink();
  d.username = "mikey";
  d.tick_time_series = "0:3 100:4";

  HttpCall call = make_builder().drink_post(d, 1700000007, 3607);

  CHECK(body_has(call.body, "username=mikey"));
  // Spaces and colons must be percent-encoded in a form body.
  CHECK(body_has(call.body, "tick_time_series=0%3A3%20100%3A4"));
}

TEST(meter_name_is_url_encoded) {
  DrinkReport d = make_drink();
  d.meter_name = "my kegboard/flow0";

  HttpCall call = make_builder().drink_post(d, 1700000007, 3607);

  CHECK_EQ(call.url, std::string("https://kegbot.example.com/api/taps/my%20kegboard%2Fflow0"));
}

TEST(thermo_post_targets_sensor_endpoint) {
  ThermoReport t;
  t.sensor_name = "thermo-28ff641d8fbb0517";
  t.temp_c = 4.25f;
  t.when_unix = 1700000000;
  t.when_uptime_s = 3600;

  HttpCall call = make_builder().thermo_post(t, 1700000005, 3605);

  CHECK_EQ(call.method, std::string("POST"));
  CHECK_EQ(call.url, std::string("https://kegbot.example.com/api/thermo-sensors/thermo-28ff641d8fbb0517"));
  CHECK(body_has(call.body, "temp_c=4.250"));
  CHECK(body_has(call.body, "when=1700000000"));
  CHECK(body_has(call.body, "now=1700000005"));
}

TEST(thermo_post_handles_negative_temperature) {
  ThermoReport t;
  t.sensor_name = "freezer";
  t.temp_c = -18.5f;
  t.when_unix = 1700000000;

  HttpCall call = make_builder().thermo_post(t, 1700000000, 10);
  CHECK(body_has(call.body, "temp_c=-18.500"));
}

TEST(auth_token_get_builds_lookup_url) {
  HttpCall call = make_builder().auth_token_get("core.rfid", "deadbeef");

  CHECK_EQ(call.method, std::string("GET"));
  CHECK_EQ(call.url, std::string("https://kegbot.example.com/api/auth-tokens/core.rfid/deadbeef"));
  CHECK_EQ(call.body, std::string(""));
}

TEST(url_encode_leaves_unreserved_characters_alone) {
  CHECK_EQ(kegboard::url_encode("abcXYZ019-_.~"), std::string("abcXYZ019-_.~"));
}

TEST(url_encode_escapes_reserved_and_high_bytes) {
  CHECK_EQ(kegboard::url_encode("a b&c=d"), std::string("a%20b%26c%3Dd"));
  CHECK_EQ(kegboard::url_encode("\xff"), std::string("%FF"));
}

TEST(format_float_handles_non_finite_values) {
  // A disconnected DS18B20 can produce NaN; it must not corrupt the body.
  CHECK_EQ(kegboard::format_float(NAN, 3), std::string("0"));
  CHECK_EQ(kegboard::format_float(INFINITY, 3), std::string("0"));
}

}  // namespace

TEST_MAIN("kegbot_request", {
  RUN(base_url_trailing_slash_is_normalized);
  RUN(base_url_ending_in_api_is_normalized);
  RUN(drink_post_targets_tap_endpoint);
  RUN(drink_post_includes_ticks_and_duration);
  RUN(drink_post_omits_volume_by_default);
  RUN(drink_post_includes_volume_when_enabled);
  RUN(drink_post_uses_wall_clock_when_synced);
  RUN(drink_post_falls_back_to_uptime_when_clock_never_synced);
  RUN(drink_post_falls_back_when_pour_predates_sync);
  RUN(queued_pour_reports_correct_elapsed_time);
  RUN(drink_post_omits_empty_username_and_series);
  RUN(drink_post_includes_username_and_series_when_present);
  RUN(meter_name_is_url_encoded);
  RUN(thermo_post_targets_sensor_endpoint);
  RUN(thermo_post_handles_negative_temperature);
  RUN(auth_token_get_builds_lookup_url);
  RUN(url_encode_leaves_unreserved_characters_alone);
  RUN(url_encode_escapes_reserved_and_high_bytes);
  RUN(format_float_handles_non_finite_values);
})
