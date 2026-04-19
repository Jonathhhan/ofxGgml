#pragma once

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

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

	ofJson()
		: m_type(Type::Null) {}

	static ofJson parse(const std::string & text, void * = nullptr, bool = true) {
		const std::string trimmed = trim(text);
		if (trimmed.empty()) return ofJson();

		if (trimmed.front() == '[' && trimmed.back() == ']') {
			ofJson arr;
			arr.m_type = Type::Array;
			const std::string body = trim(trimmed.substr(1, trimmed.size() - 2));
			if (body.empty()) return arr;

			size_t start = 0;
			while (start < body.size()) {
				size_t comma = body.find(',', start);
				if (comma == std::string::npos) comma = body.size();
				const std::string token = trim(body.substr(start, comma - start));
				char * end = nullptr;
				const double value = std::strtod(token.c_str(), &end);
				if (end == token.c_str() || (end && *end != '\0')) {
					ofJson discarded;
					discarded.m_type = Type::Discarded;
					return discarded;
				}
				ofJson num;
				num.m_type = Type::Number;
				num.m_number = value;
				arr.m_array.push_back(num);
				start = comma + 1;
			}
			return arr;
		}

		if (trimmed.front() == '{' && trimmed.back() == '}') {
			ofJson obj;
			obj.m_type = Type::Object;
			return obj;
		}

		char * end = nullptr;
		const double scalar = std::strtod(trimmed.c_str(), &end);
		if (end != trimmed.c_str() && end && *end == '\0') {
			ofJson num;
			num.m_type = Type::Number;
			num.m_number = scalar;
			return num;
		}

		ofJson discarded;
		discarded.m_type = Type::Discarded;
		return discarded;
	}

	bool is_discarded() const { return m_type == Type::Discarded; }
	bool is_array() const { return m_type == Type::Array; }
	bool is_object() const { return m_type == Type::Object; }
	bool is_boolean() const { return m_type == Type::Bool; }
	bool is_number() const { return m_type == Type::Number; }
	bool is_number_integer() const { return m_type == Type::Number; }
	bool is_string() const { return m_type == Type::String; }

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

	const ofJson & operator[](const std::string & key) const {
		auto it = m_object.find(key);
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

	void push_back(const ofJson & value) {
		if (m_type != Type::Array) {
			m_type = Type::Array;
			m_array.clear();
		}
		m_array.push_back(value);
	}

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
		case Type::String:
			return "\"" + m_string + "\"";
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

	std::vector<ofJson>::const_iterator begin() const { return m_array.begin(); }
	std::vector<ofJson>::const_iterator end() const { return m_array.end(); }

private:
	static const ofJson & nullValue() {
		static const ofJson v;
		return v;
	}

	static std::string trim(const std::string & s) {
		size_t b = 0;
		while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
		size_t e = s.size();
		while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
		return s.substr(b, e - b);
	}

	Type m_type = Type::Null;
	bool m_bool = false;
	double m_number = 0.0;
	std::string m_string;
	std::vector<ofJson> m_array;
	std::unordered_map<std::string, ofJson> m_object;
};
