
#ifndef JSON_HPP
#define JSON_HPP

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdexcept>
#include <cctype>
#include <cmath>

#include "common.hpp"

using std::string;
using std::map;
using std::vector;
using std::function;
using std::runtime_error;

namespace json {

	class Value {
	public:

		enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
		union {
			bool b;
			double n;
			string * s;
			vector<Value *> * a;
			map<string, Value *> * o;
		};

		Value() : type(Type::Null) { }
		Value(Type t) : type(t) { }
		Value(Type t, bool bv) : type(t), b(bv) { }

		Value(const Value &) = delete;
		Value & operator=(const Value &) = delete;

		Value(Value && other) noexcept : type(other.type) {
			move_union_from(other);
		}

		Value & operator=(Value && other) noexcept {
			if (this != &other) {
				destroy_union();
				type = other.type;
				move_union_from(other);
			}
			return * this;
		}

		~Value() { destroy_union(); }

		Value * operator[](const string & key) const {
			if (type != Type::Object || !o) return nullptr;
			auto it = o -> find(key);
			return (it != o -> end()) ? it -> second : nullptr;
		}

		Value * operator[](size_t i) const {
			if (type != Type::Array || !a || i < 0 || i >= a -> size()) return nullptr;
			return (* a)[i];
		}

		template <class TYPE> TYPE as() const;

	private:

		void move_union_from(Value & other) {
			switch (type) {
				case Type::Bool:   b = other.b; break;
				case Type::Number: n = other.n; break;
				case Type::String: s = other.s; other.s = nullptr; break;
				case Type::Array:  a = other.a; other.a = nullptr; break;
				case Type::Object: o = other.o; other.o = nullptr; break;
				default: break;
			}
			other.type = Type::Null;
		}
	
		void destroy_union() {
			switch (type) {
				case Type::String: delete s; break;
				case Type::Array:
					if (a) {
						for (Value * v : * a) delete v;
						delete a;
					}
				break;
				case Type::Object:
					if (o) {
						for (auto & [_key, v] : * o) delete v;
						delete o;
					}
				break;
				default: break;
			}
		}

	};

	template <> int Value::as<int>() const {
		if (type != Type::Number) throw runtime_error("value is not a number");
		return (int)n;
	}

	template <> double Value::as<double>() const {
		if (type != Type::Number) throw runtime_error("value is not a number");
		return n;
	}

	template <> bool Value::as<bool>() const {
		if (type != Type::Bool) throw runtime_error("value is not a boolean");
		return b;
	}

	template <> string Value::as<string>() const {
		if (type != Type::String) throw runtime_error("value is not a boolean");
		return string(* s);
	}

	enum TokenType {
		_NULL, _TRUE, _FALSE, _NUMBER, _STRING,
		LBRACE, RBRACE, LBRACKET, RBRACKET,
		COLON, COMMA
	};

	struct Token {
		TokenType type;
		string lexeme;
	};

	class JSON {
	public:

		static vector<Token> tokenize(const string & json) {
			vector<Token> tokens;
			size_t i = 0;
			while (i < json.length()) {
				char c = json[i];
				if (isspace((unsigned char) c)) { i++; continue; }
				switch (c) {
					case '{': tokens.push_back({ LBRACE,   "{" }); i++; break;
					case '}': tokens.push_back({ RBRACE,   "}" }); i++; break;
					case '[': tokens.push_back({ LBRACKET, "[" }); i++; break;
					case ']': tokens.push_back({ RBRACKET, "]" }); i++; break;
					case ':': tokens.push_back({ COLON,    ":" }); i++; break;
					case ',': tokens.push_back({ COMMA,    "," }); i++; break;
					case '"': {
						string value;
						i++; // skip opening quote
						bool closed = false;
						while (i < json.size()) {
							char ch = json[i];
							if (ch == '"') { i++; closed = true; break; }
							if (ch == '\\' && i + 1 < json.size()) {
								char esc = json[i + 1];
								switch (esc) {
									case '"':  value += '"';  i += 2; break;
									case '\\': value += '\\'; i += 2; break;
									case '/':  value += '/';  i += 2; break;
									case 'b':  value += '\b'; i += 2; break;
									case 'f':  value += '\f'; i += 2; break;
									case 'n':  value += '\n'; i += 2; break;
									case 'r':  value += '\r'; i += 2; break;
									case 't':  value += '\t'; i += 2; break;
									case 'u': {
										if (i + 6 > json.size())
											throw runtime_error("invalid \\u escape in string");
										string hex = json.substr(i + 2, 4);
										unsigned int codepoint = std::stoul(hex, nullptr, 16);
										// minimal UTF-8 encoding of the BMP codepoint (no surrogate pairs)
										if (codepoint < 0x80) {
											value += (char) codepoint;
										} else if (codepoint < 0x800) {
											value += (char) (0xC0 | (codepoint >> 6));
											value += (char) (0x80 | (codepoint & 0x3F));
										} else {
											value += (char) (0xE0 | (codepoint >> 12));
											value += (char) (0x80 | ((codepoint >> 6) & 0x3F));
											value += (char) (0x80 | (codepoint & 0x3F));
										}
										i += 6;
									} break;
									default: throw runtime_error("invalid escape sequence in string");
								}
							} else {
								value += ch;
								i++;
							}
						}
						if (!closed) throw runtime_error("unterminated string in JSON input");
						tokens.push_back({ _STRING, value });
					} break;
					default:
						if (isdigit((unsigned char) c) || c == '-') {
							size_t start = i;
							if (json[i] == '-') i++;
							while (i < json.size() && isdigit((unsigned char) json[i])) i++;
							if (i < json.size() && json[i] == '.') {
								i++;
								while (i < json.size() && isdigit((unsigned char) json[i])) i++;
							}
							if (i < json.size() && (json[i] == 'e' || json[i] == 'E')) {
								i++;
								if (i < json.size() && (json[i] == '+' || json[i] == '-')) i++;
								while (i < json.size() && isdigit((unsigned char) json[i])) i++;
							}
							tokens.push_back({ _NUMBER, json.substr(start, i - start) });
						} else if (json.compare(i, 4, "true") == 0) {
							tokens.push_back({ _TRUE, "true" });
							i += 4;
						} else if (json.compare(i, 5, "false") == 0) {
							tokens.push_back({ _FALSE, "false" });
							i += 5;
						} else if (json.compare(i, 4, "null") == 0) {
							tokens.push_back({ _NULL, "null" });
							i += 4;
						} else throw runtime_error("invalid JSON input");
					break;
				}
			}
			return tokens;
		}

		static Value * parse(const string & json) {

			vector<Token> tokens = tokenize(json);
			size_t i = 0;
			auto is_at_end = [&]() { return i >= tokens.size(); };
			auto peek = [&]() -> const Token & {
				if (is_at_end()) throw runtime_error("unexpected end of JSON input");
				return tokens[i];
			};
			auto advance = [&]() -> const Token & {
				if (is_at_end()) throw runtime_error("unexpected end of JSON input");
				return tokens[i++];
			};
			auto match = [&](TokenType type) {
				if (!is_at_end() && peek().type == type) {
					advance();
					return true;
				}
				return false;
			};
			auto consume = [&](TokenType type, const string & message) {
				if (match(type)) return;
				throw runtime_error(message);
			};

			function<Value *()> value, array, object;

			// value = object | array | literal ;
			value = [&]() -> Value * {
				switch (peek().type) {
					case LBRACE:   return object();
					case LBRACKET: return array();
					case _STRING: {
						string lexeme = advance().lexeme;
						Value * v = new Value(Value::Type::String);
						v -> s = new string(std::move(lexeme));
						return v;
					}
					case _NUMBER: {
						string lexeme = advance().lexeme;
						Value * v = new Value(Value::Type::Number);
						v -> n = std::stod(lexeme);
						return v;
					}
					case _TRUE:
						advance();
						return new Value(Value::Type::Bool, true);
					case _FALSE:
						advance();
						return new Value(Value::Type::Bool, false);
					case _NULL:
						advance();
						return new Value(Value::Type::Null);
					default: throw runtime_error("unexpected token in JSON input");
				}
			};

			// array = '[' [ value { ',' value } ] ']' ;
			array = [&]() -> Value * {
				consume(LBRACKET, "expected '[' at start of array");
				vector<Value *> elements;
				try {
					if (!match(RBRACKET)) {
						do elements.push_back(value());
						while (match(COMMA));
						consume(RBRACKET, "expected ']' at end of array");
					}
				} catch (...) {
					for (Value * v : elements) delete v;
					throw;
				}
				Value * v = new Value(Value::Type::Array);
				v -> a = new vector<Value *>(std::move(elements));
				return v;
			};

			// object = '{' [ string ':' value { ',' string ':' value } ] '}' ;
			object = [&]() -> Value * {
				consume(LBRACE, "expected '{' at start of object");
				map<string, Value *> members;
				try {
					if (!match(RBRACE)) {
						do {
							consume(_STRING, "expected string key in object");
							string key = tokens[i - 1].lexeme;
							consume(COLON, "expected ':' after key in object");
							Value * val = value();
							// a repeated key would otherwise leak the value that gets
							// overwritten; JSON semantics say "last one wins", so we
							// honor that but free the discarded value first.
							auto it = members.find(key);
							if (it != members.end()) {
								delete it->second;
								it->second = val;
							} else members.emplace(std::move(key), val);
						} while (match(COMMA));
						consume(RBRACE, "expected '}' at end of object");
					}
				} catch (...) {
					for (auto & [_key, v] : members) delete v;
					throw;
				}
				Value * v = new Value(Value::Type::Object);
				v -> o = new map<string, Value *>(std::move(members));
				return v;
			};
			Value * result = value();
			if (!is_at_end()) {
				delete result;
				throw runtime_error("unexpected trailing content after JSON value");
			}
			return result;
		}

		static string stringify(Value * value, string indent = "", bool pretty = false) {
			return stringify(value, indent, pretty, 0);
		}

		private:

		static string stringify(Value * value, const string & indent, bool pretty, size_t depth) {
			switch (value -> type) {
				case Value::Type::Null: return "null";
				case Value::Type::Bool: return value -> b ? "true" : "false";
				case Value::Type::Number: return formatNumber(value -> n);
				case Value::Type::String: return "\"" + escape(*(value -> s)) + "\"";
				case Value::Type::Array: {
					if (value -> a -> empty()) return "[]";
					string res = "[";
					if (pretty) res += "\n";
					for (size_t idx = 0; idx < value -> a -> size(); ++idx) {
						if (idx > 0) {
							res += ",";
							if (pretty) res += "\n";
						}
						if (pretty) res += indentation(depth + 1, indent);
						res += stringify((* value -> a)[idx], indent, pretty, depth + 1);
					}
					if (pretty) { res += "\n"; res += indentation(depth, indent); }
					res += "]";
					return res;
				}
				case Value::Type::Object: {
					if (value -> o -> empty()) return "{}";
					string res = "{";
					if (pretty) res += "\n";
					bool first = true;
					for (const auto & [key, val] : *(value -> o)) {
						if (!first) {
							res += ",";
							if (pretty) res += "\n";
						}
						first = false;
						if (pretty) res += indentation(depth + 1, indent);
						res += "\"" + escape(key) + "\":";
						if (pretty) res.push_back(' ');
						res += stringify(val, indent, pretty, depth + 1);
					}
					if (pretty) { res += "\n"; res += indentation(depth, indent); }
					res += "}";
					return res;
				}
			}
			return "null"; // unreachable, silences -Wreturn-type
		}

		static string formatNumber(double n) {
			if (std::floor(n) == n && std::abs(n) < 1e15)
				return itoa((long long) n);
			char buf[64];
			snprintf(buf, sizeof(buf), "%.17g", n);
			return string(buf);
		}

		static string escape(const string & s) {
			string out;
			out.reserve(s.size());
			for (unsigned char c : s) {
				switch (c) {
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\b': out += "\\b";  break;
					case '\f': out += "\\f";  break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					case '\t': out += "\\t";  break;
					default:
						if (c < 0x20) {
							char buf[8];
							snprintf(buf, sizeof(buf), "\\u%04x", c);
							out += buf;
						} else out.push_back((char)c);
				}
			}
			return out;
		}

		static string indentation(size_t depth, const string & indent) {
			string result;
			while (depth--) result += indent;
			return result;
		}

	};

}

#endif