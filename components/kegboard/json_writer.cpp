#include "json_writer.h"

#include <cmath>
#include <cstdio>

namespace kbcore {

void JsonWriter::element_prefix_() {
  if (this->after_key_) {
    // Value directly follows its key; no comma.
    this->after_key_ = false;
    return;
  }
  if (!this->has_element_.empty()) {
    if (this->has_element_.back())
      this->out_ += ',';
    this->has_element_.back() = true;
  }
}

void JsonWriter::begin_object() {
  this->element_prefix_();
  this->out_ += '{';
  this->has_element_.push_back(false);
}

void JsonWriter::end_object() {
  this->has_element_.pop_back();
  this->out_ += '}';
}

void JsonWriter::begin_array() {
  this->element_prefix_();
  this->out_ += '[';
  this->has_element_.push_back(false);
}

void JsonWriter::end_array() {
  this->has_element_.pop_back();
  this->out_ += ']';
}

void JsonWriter::key(const char *name) {
  this->element_prefix_();
  this->out_ += '"';
  this->append_escaped_(name);
  this->out_ += "\":";
  this->after_key_ = true;
}

void JsonWriter::value(const std::string &v) { this->value(v.c_str()); }

void JsonWriter::value(const char *v) {
  this->element_prefix_();
  this->out_ += '"';
  this->append_escaped_(v);
  this->out_ += '"';
}

void JsonWriter::value(bool v) {
  this->element_prefix_();
  this->out_ += v ? "true" : "false";
}

void JsonWriter::value(uint32_t v) {
  this->element_prefix_();
  this->out_ += std::to_string(v);
}

void JsonWriter::value(int32_t v) {
  this->element_prefix_();
  this->out_ += std::to_string(v);
}

void JsonWriter::value(double v, int decimals) {
  this->element_prefix_();
  if (std::isnan(v) || std::isinf(v)) {
    this->out_ += '0';
    return;
  }
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
    this->out_ += '0';
    return;
  }
  this->out_.append(buf, n);
}

void JsonWriter::raw_value(const std::string &json) {
  this->element_prefix_();
  this->out_ += json;
}

void JsonWriter::append_escaped_(const char *s) {
  for (const char *p = s; *p != '\0'; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    switch (c) {
      case '"':
        this->out_ += "\\\"";
        break;
      case '\\':
        this->out_ += "\\\\";
        break;
      case '\n':
        this->out_ += "\\n";
        break;
      case '\r':
        this->out_ += "\\r";
        break;
      case '\t':
        this->out_ += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          this->out_ += buf;
        } else {
          // UTF-8 multibyte sequences pass through untouched.
          this->out_ += static_cast<char>(c);
        }
        break;
    }
  }
}

void JsonWriter::clear() {
  this->out_.clear();
  this->has_element_.clear();
  this->after_key_ = false;
}

}  // namespace kbcore
