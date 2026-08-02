#include "kegbot_request.h"

#include <cmath>
#include <cstdio>

namespace kbcore {

namespace {

bool ends_with(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void append_field(std::string *body, const char *key, const std::string &value) {
  if (!body->empty())
    *body += '&';
  *body += key;
  *body += '=';
  *body += url_encode(value);
}

void append_field(std::string *body, const char *key, uint32_t value) {
  append_field(body, key, std::to_string(value));
}

}  // namespace

std::string url_encode(const std::string &value) {
  static const char HEX[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += HEX[c >> 4];
      out += HEX[c & 0x0f];
    }
  }
  return out;
}

std::string format_float(float value, int decimals) {
  if (std::isnan(value) || std::isinf(value))
    return "0";
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(value));
  if (n < 0)
    return "0";
  return std::string(buf, static_cast<size_t>(n) < sizeof(buf) ? n : sizeof(buf) - 1);
}

void KegbotRequestBuilder::set_base_url(const std::string &base_url) {
  std::string url = base_url;
  while (!url.empty() && url.back() == '/')
    url.pop_back();
  // Accept a URL that already points at the API root, so users who paste
  // either form from the Kegbot admin page get a working config.
  if (ends_with(url, "/api"))
    url.resize(url.size() - 4);
  this->base_url_ = url;
}

HttpCall KegbotRequestBuilder::drink_post(const DrinkReport &report, uint32_t now_unix, uint32_t now_uptime_s) const {
  HttpCall call;
  call.method = "POST";
  call.url = this->base_url_ + "/api/taps/" + url_encode(report.meter_name);

  append_field(&call.body, "ticks", report.ticks);
  append_field(&call.body, "duration", report.duration_s);

  if (this->send_volume_)
    append_field(&call.body, "volume_ml", format_float(report.volume_ml, 3));

  // Only the difference between these two is used by the server, so send a
  // consistent pair: wall time if the pour was stamped with a synced clock,
  // monotonic uptime otherwise.
  if (report.pour_time_unix != 0 && now_unix != 0) {
    append_field(&call.body, "pour_time", report.pour_time_unix);
    append_field(&call.body, "now", now_unix);
  } else {
    append_field(&call.body, "pour_time", report.pour_uptime_s);
    append_field(&call.body, "now", now_uptime_s);
  }

  if (!report.username.empty())
    append_field(&call.body, "username", report.username);
  if (!report.tick_time_series.empty())
    append_field(&call.body, "tick_time_series", report.tick_time_series);

  return call;
}

HttpCall KegbotRequestBuilder::thermo_post(const ThermoReport &report, uint32_t now_unix, uint32_t now_uptime_s) const {
  HttpCall call;
  call.method = "POST";
  call.url = this->base_url_ + "/api/thermo-sensors/" + url_encode(report.sensor_name);

  append_field(&call.body, "temp_c", format_float(report.temp_c, 3));

  if (report.when_unix != 0 && now_unix != 0) {
    append_field(&call.body, "when", report.when_unix);
    append_field(&call.body, "now", now_unix);
  } else {
    append_field(&call.body, "when", report.when_uptime_s);
    append_field(&call.body, "now", now_uptime_s);
  }

  return call;
}

HttpCall KegbotRequestBuilder::auth_token_get(const std::string &device, const std::string &token) const {
  HttpCall call;
  call.method = "GET";
  call.url = this->base_url_ + "/api/auth-tokens/" + url_encode(device) + "/" + url_encode(token);
  return call;
}

}  // namespace kbcore
