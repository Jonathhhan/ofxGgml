#pragma once

#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Minimal ofJson / nlohmann::json stub for headless unit tests.
// Supports the subset of the nlohmann::json API used by addon sources,
// including the nlohmann-style initializer-list object-construction syntax:
//   json = {{"key", value}, {"key2", value2}};

class ofJson {
public:
enum class Type {
Discarded,
Null,
Bool,
Number,
String,
Array,
Object
};

// --- Constructors ---

ofJson() : m_type(Type::Null) {}
ofJson(const ofJson &) = default;
ofJson(ofJson &&) = default;

// Implicit construction from string types
ofJson(const std::string & value) : m_type(Type::String), m_string(value) {}  // NOLINT
ofJson(std::string && value) : m_type(Type::String), m_string(std::move(value)) {}  // NOLINT
ofJson(const char * value) : m_type(Type::String), m_string(value ? value : "") {}  // NOLINT

// Implicit construction from bool (before arithmetic to avoid ambiguity)
ofJson(bool value) : m_type(Type::Bool), m_bool(value) {}  // NOLINT

// Implicit construction from arithmetic types (excluding bool)
template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
ofJson(T value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}  // NOLINT

// Initializer-list constructor: mimics nlohmann::json behavior.
// If all elements are 2-element arrays whose first element is a string,
// the result is an object; otherwise it is an array.
ofJson(std::initializer_list<ofJson> items) {  // NOLINT
bool looksLikeObject = (items.size() > 0);
for (const auto & item : items) {
if (!item.is_array() || item.m_array.size() != 2 ||
!item.m_array[0].is_string()) {
looksLikeObject = false;
break;
}
}
if (looksLikeObject) {
m_type = Type::Object;
for (const auto & item : items) {
m_object[item.m_array[0].m_string] = item.m_array[1];
}
} else {
m_type = Type::Array;
for (const auto & item : items) {
m_array.push_back(item);
}
}
}

// --- Static factories ---

static ofJson parse(const std::string & text, void * = nullptr, bool = true) {
const std::string trimmed = trimStr(text);
if (trimmed.empty()) {
throw std::invalid_argument("ofJson::parse: empty input");
}
size_t pos = 0;
const ofJson result = parseValue(trimmed, pos);
// Skip trailing whitespace
while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
++pos;
}
return result;
}

static ofJson array() {
ofJson json;
json.m_type = Type::Array;
return json;
}

static ofJson object() {
ofJson json;
json.m_type = Type::Object;
return json;
}

// --- Type checks ---

bool is_discarded() const { return m_type == Type::Discarded; }
bool is_array() const { return m_type == Type::Array; }
bool is_object() const { return m_type == Type::Object; }
bool is_boolean() const { return m_type == Type::Bool; }
bool is_number() const { return m_type == Type::Number; }
bool is_number_integer() const { return m_type == Type::Number; }
bool is_string() const { return m_type == Type::String; }

// --- Value getters ---

template<typename T>
T get() const {
if constexpr (std::is_same_v<T, std::string>) {
return m_string;
} else if constexpr (std::is_same_v<T, bool>) {
return m_bool;
} else {
return static_cast<T>(m_number);
}
}

bool contains(const std::string & key) const {
return m_type == Type::Object && m_object.find(key) != m_object.end();
}

// --- Subscript operators ---

const ofJson & operator[](const std::string & key) const {
const auto it = m_object.find(key);
return it != m_object.end() ? it->second : nullValue();
}

ofJson & operator[](const std::string & key) {
if (m_type != Type::Object) {
m_type = Type::Object;
m_object.clear();
}
return m_object[key];
}

const ofJson & operator[](size_t index) const {
if (index < m_array.size()) return m_array[index];
return nullValue();
}

ofJson & operator[](size_t index) {
if (m_type != Type::Array) {
m_type = Type::Array;
m_array.clear();
}
if (index >= m_array.size()) {
m_array.resize(index + 1);
}
return m_array[index];
}

// --- value() helpers ---

template<typename T>
T value(const std::string & key, const T & fallback) const {
const auto it = m_object.find(key);
if (it == m_object.end()) return fallback;
if constexpr (std::is_same_v<T, std::string>) {
if (it->second.m_type == Type::String) return it->second.m_string;
return fallback;
}
if constexpr (std::is_arithmetic_v<T>) {
if (it->second.m_type == Type::Number) return static_cast<T>(it->second.m_number);
if (it->second.m_type == Type::Bool) return static_cast<T>(it->second.m_bool ? 1 : 0);
return fallback;
}
return fallback;
}

std::string value(const std::string & key, const char * fallback) const {
const auto it = m_object.find(key);
if (it == m_object.end()) return std::string(fallback ? fallback : "");
if (it->second.m_type == Type::String) return it->second.m_string;
return std::string(fallback ? fallback : "");
}

// --- Mutation ---

void push_back(const ofJson & value) {
if (m_type != Type::Array) {
m_type = Type::Array;
m_array.clear();
}
m_array.push_back(value);
}

// --- Assignment operators ---

ofJson & operator=(const ofJson &) = default;
ofJson & operator=(ofJson &&) = default;

template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
ofJson & operator=(T value) {
m_type = Type::Number;
m_number = static_cast<double>(value);
return *this;
}

ofJson & operator=(bool value) {
m_type = Type::Bool;
m_bool = value;
return *this;
}

ofJson & operator=(const std::string & value) {
m_type = Type::String;
m_string = value;
return *this;
}

ofJson & operator=(const char * value) {
m_type = Type::String;
m_string = value ? value : "";
return *this;
}

ofJson & operator=(std::initializer_list<ofJson> items) {
*this = ofJson(items);
return *this;
}

// --- Serialization ---

std::string dump(int = -1) const {
switch (m_type) {
case Type::Discarded:
case Type::Null:
return "null";
case Type::Bool:
return m_bool ? "true" : "false";
case Type::Number: {
std::ostringstream out;
out << m_number;
return out.str();
}
case Type::String: {
std::string result = "\"";
for (char c : m_string) {
if (c == '"') result += "\\\"";
else if (c == '\\') result += "\\\\";
else if (c == '\n') result += "\\n";
else if (c == '\r') result += "\\r";
else if (c == '\t') result += "\\t";
else result += c;
}
result += '"';
return result;
}
case Type::Array: {
std::ostringstream out;
out << "[";
for (size_t i = 0; i < m_array.size(); ++i) {
if (i > 0) out << ",";
out << m_array[i].dump();
}
out << "]";
return out.str();
}
case Type::Object: {
std::ostringstream out;
out << "{";
bool first = true;
for (const auto & item : m_object) {
if (!first) out << ",";
first = false;
out << "\"" << item.first << "\":" << item.second.dump();
}
out << "}";
return out.str();
}
}
return "null";
}

// --- Iteration (over array elements) ---

std::vector<ofJson>::const_iterator begin() const { return m_array.begin(); }
std::vector<ofJson>::const_iterator end() const { return m_array.end(); }

private:
static const ofJson & nullValue() {
static const ofJson v;
return v;
}

static std::string trimStr(const std::string & s) {
size_t b = 0;
while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
size_t e = s.size();
while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
return s.substr(b, e - b);
}

// --- Recursive descent parser ---

static void skipWs(const std::string & s, size_t & pos) {
while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
}

static ofJson parseValue(const std::string & s, size_t & pos) {
skipWs(s, pos);
if (pos >= s.size()) {
throw std::invalid_argument("ofJson::parse: unexpected end of input");
}
char c = s[pos];
if (c == '"') return parseString(s, pos);
if (c == '{') return parseObject(s, pos);
if (c == '[') return parseArray(s, pos);
if (c == 't') {
if (s.substr(pos, 4) == "true") { pos += 4; ofJson j; j.m_type = Type::Bool; j.m_bool = true; return j; }
}
if (c == 'f') {
if (s.substr(pos, 5) == "false") { pos += 5; ofJson j; j.m_type = Type::Bool; j.m_bool = false; return j; }
}
if (c == 'n') {
if (s.substr(pos, 4) == "null") { pos += 4; return ofJson(); }
}
if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(s, pos);
throw std::invalid_argument(std::string("ofJson::parse: unexpected character '") + c + "'");
}

static std::string parseRawString(const std::string & s, size_t & pos) {
if (pos >= s.size() || s[pos] != '"') {
throw std::invalid_argument("ofJson::parse: expected '\"'");
}
++pos; // skip opening "
std::string result;
while (pos < s.size()) {
char c = s[pos];
if (c == '"') { ++pos; return result; }
if (c == '\\') {
++pos;
if (pos >= s.size()) break;
char esc = s[pos++];
switch (esc) {
case '"': result += '"'; break;
case '\\': result += '\\'; break;
case '/': result += '/'; break;
case 'b': result += '\b'; break;
case 'f': result += '\f'; break;
case 'n': result += '\n'; break;
case 'r': result += '\r'; break;
case 't': result += '\t'; break;
case 'u': {
// Skip 4 hex digits (simplified: treat as '?')
for (int i = 0; i < 4 && pos < s.size(); ++i) ++pos;
result += '?';
break;
}
default: result += esc; break;
}
} else {
result += c;
++pos;
}
}
throw std::invalid_argument("ofJson::parse: unterminated string");
}

static ofJson parseString(const std::string & s, size_t & pos) {
ofJson j;
j.m_type = Type::String;
j.m_string = parseRawString(s, pos);
return j;
}

static ofJson parseObject(const std::string & s, size_t & pos) {
if (pos >= s.size() || s[pos] != '{') {
throw std::invalid_argument("ofJson::parse: expected '{'");
}
++pos;
ofJson j;
j.m_type = Type::Object;
skipWs(s, pos);
if (pos < s.size() && s[pos] == '}') { ++pos; return j; }
while (pos < s.size()) {
skipWs(s, pos);
std::string key = parseRawString(s, pos);
skipWs(s, pos);
if (pos >= s.size() || s[pos] != ':') {
throw std::invalid_argument("ofJson::parse: expected ':'");
}
++pos;
ofJson value = parseValue(s, pos);
j.m_object[std::move(key)] = std::move(value);
skipWs(s, pos);
if (pos >= s.size()) break;
if (s[pos] == '}') { ++pos; return j; }
if (s[pos] == ',') { ++pos; continue; }
throw std::invalid_argument("ofJson::parse: expected ',' or '}'");
}
throw std::invalid_argument("ofJson::parse: unterminated object");
}

static ofJson parseArray(const std::string & s, size_t & pos) {
if (pos >= s.size() || s[pos] != '[') {
throw std::invalid_argument("ofJson::parse: expected '['");
}
++pos;
ofJson j;
j.m_type = Type::Array;
skipWs(s, pos);
if (pos < s.size() && s[pos] == ']') { ++pos; return j; }
while (pos < s.size()) {
ofJson value = parseValue(s, pos);
j.m_array.push_back(std::move(value));
skipWs(s, pos);
if (pos >= s.size()) break;
if (s[pos] == ']') { ++pos; return j; }
if (s[pos] == ',') { ++pos; continue; }
throw std::invalid_argument("ofJson::parse: expected ',' or ']'");
}
throw std::invalid_argument("ofJson::parse: unterminated array");
}

static ofJson parseNumber(const std::string & s, size_t & pos) {
const size_t start = pos;
if (pos < s.size() && s[pos] == '-') ++pos;
while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
if (pos < s.size() && s[pos] == '.') {
++pos;
while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
}
if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
++pos;
if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
}
const std::string numStr = s.substr(start, pos - start);
char * end = nullptr;
const double value = std::strtod(numStr.c_str(), &end);
ofJson j;
j.m_type = Type::Number;
j.m_number = value;
return j;
}

Type m_type = Type::Null;
bool m_bool = false;
double m_number = 0.0;
std::string m_string;
std::vector<ofJson> m_array;
std::unordered_map<std::string, ofJson> m_object;
};
