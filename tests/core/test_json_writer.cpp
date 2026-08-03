#include "json_writer.h"

#include "test_support.h"

using kbcore::JsonWriter;

namespace {

TEST(empty_object_and_array) {
  JsonWriter w;
  w.begin_object();
  w.end_object();
  CHECK_EQ(w.str(), std::string("{}"));

  w.clear();
  w.begin_array();
  w.end_array();
  CHECK_EQ(w.str(), std::string("[]"));
}

TEST(object_with_mixed_values) {
  JsonWriter w;
  w.begin_object();
  w.key("s");
  w.value("hi");
  w.key("u");
  w.value(static_cast<uint32_t>(42));
  w.key("i");
  w.value(static_cast<int32_t>(-7));
  w.key("b");
  w.value(true);
  w.key("f");
  w.value(3.25, 2);
  w.end_object();
  CHECK_EQ(w.str(), std::string(R"({"s":"hi","u":42,"i":-7,"b":true,"f":3.25})"));
}

TEST(nested_containers_get_commas_right) {
  JsonWriter w;
  w.begin_object();
  w.key("a");
  w.begin_array();
  w.value(static_cast<uint32_t>(1));
  w.begin_object();
  w.key("x");
  w.value(static_cast<uint32_t>(2));
  w.end_object();
  w.value(static_cast<uint32_t>(3));
  w.end_array();
  w.key("b");
  w.value(static_cast<uint32_t>(4));
  w.end_object();
  CHECK_EQ(w.str(), std::string(R"({"a":[1,{"x":2},3],"b":4})"));
}

TEST(string_escaping) {
  JsonWriter w;
  w.begin_object();
  w.key("v");
  w.value("a\"b\\c\nd\te\rf");
  w.end_object();
  CHECK_EQ(w.str(), std::string(R"({"v":"a\"b\\c\nd\te\rf"})"));
}

TEST(control_characters_escape_as_unicode) {
  JsonWriter w;
  w.begin_array();
  w.value(std::string("x\x01y"));
  w.end_array();
  CHECK_EQ(w.str(), std::string("[\"x\\u0001y\"]"));
}

TEST(utf8_passes_through) {
  JsonWriter w;
  w.begin_array();
  w.value("bi\xc3\xa8re");
  w.end_array();
  CHECK_EQ(w.str(), std::string("[\"bi\xc3\xa8re\"]"));
}

TEST(non_finite_floats_serialize_as_zero) {
  JsonWriter w;
  w.begin_array();
  w.value(static_cast<double>(NAN), 3);
  w.value(static_cast<double>(INFINITY), 3);
  w.end_array();
  CHECK_EQ(w.str(), std::string("[0,0]"));
}

TEST(raw_value_splices_with_commas) {
  JsonWriter w;
  w.begin_object();
  w.key("a");
  w.raw_value(R"({"inner":1})");
  w.key("b");
  w.value(static_cast<uint32_t>(2));
  w.end_object();
  CHECK_EQ(w.str(), std::string(R"({"a":{"inner":1},"b":2})"));
}

TEST(raw_value_in_array_gets_commas) {
  JsonWriter w;
  w.begin_array();
  w.raw_value("{}");
  w.raw_value("[1]");
  w.end_array();
  CHECK_EQ(w.str(), std::string("[{},[1]]"));
}

}  // namespace

TEST_MAIN("json_writer", {
  RUN(empty_object_and_array);
  RUN(object_with_mixed_values);
  RUN(nested_containers_get_commas_right);
  RUN(string_escaping);
  RUN(control_characters_escape_as_unicode);
  RUN(utf8_passes_through);
  RUN(non_finite_floats_serialize_as_zero);
  RUN(raw_value_splices_with_commas);
  RUN(raw_value_in_array_gets_commas);
})
