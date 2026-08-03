#pragma once

// Minimal JSON serializer.
//
// Part of the framework-agnostic kegboard core: no ESPHome, Arduino, or
// ESP-IDF headers. See CORE.md.

#include <cstdint>
#include <string>
#include <vector>

namespace kbcore {

/// Builds a JSON document into a string.
///
/// Write-only and allocation-light: enough for the event protocol, and small
/// enough to audit. The caller is responsible for structural sanity (matching
/// begin/end, keys only inside objects); the writer handles commas, quoting,
/// and escaping. Output is deterministic, which is what lets the host tests
/// diff and schema-check it.
class JsonWriter {
 public:
  void begin_object();
  void end_object();
  void begin_array();
  void end_array();

  /// Write an object key. Must be followed by exactly one value or container.
  void key(const char *name);

  void value(const std::string &v);
  void value(const char *v);
  void value(bool v);
  void value(uint32_t v);
  void value(int32_t v);
  /// Fixed-decimal float. NaN/inf serialize as 0, since JSON has no spelling
  /// for them and a corrupt reading must not corrupt the document.
  void value(double v, int decimals);

  /// Splice pre-serialized JSON in as a value, verbatim. The caller vouches
  /// that it is a complete, valid JSON value; used to embed event payloads
  /// that were rendered at creation time into the batch envelope.
  void raw_value(const std::string &json);

  /// The finished document. Valid once all containers are closed.
  const std::string &str() const { return out_; }

  void clear();

 private:
  void element_prefix_();
  void append_escaped_(const char *s);

  std::string out_;
  /// One entry per open container: whether it already has an element.
  std::vector<bool> has_element_;
  bool after_key_{false};
};

}  // namespace kbcore
