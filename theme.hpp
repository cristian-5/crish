
#ifndef COLOR_HPP
#define COLOR_HPP

#include <string>
#include <map>

#include "json.hpp"
#include "common.hpp"

using std::string;
using std::map;

class color_t {
public:
	uint32_t hex = 0;
	color_t(uint8_t r, uint8_t g, uint8_t b) : hex((r << 16) | (g << 8) | b) { }
	color_t(uint32_t hex) : hex(hex) { }
	uint8_t r() const { return (hex >> 16) & 0xFF; }
	uint8_t g() const { return (hex >> 8) & 0xFF; }
	uint8_t b() const { return hex & 0xFF; }
};

struct theme_t {
	color_t fg = 0xFFFFFF, bg = -1; // <- should work
	bool bold = false;
	bool italic = false;
	bool underline = false;
};

// these items are mandatory defaults, DO NOT REMOVE!!!
map<string, theme_t> THEME = {
	{ "failure", { 0xFF0000 } }, // red
	{ "success", { 0x00FF00 } }, // green
};

const bool BOLD = true;

string bold(const string & text) {
	return "\033[1m" + text + "\033[0m";
}

string italic(const string & text) {
	return "\033[3m" + text + "\033[0m";
}

string underline(const string & text) {
	return "\033[4m" + text + "\033[0m";
}

string fg(const string & text, color_t rgb, bool bold = false) {
	string code = "\033[";
	if (bold) code += "1;";
	code += "38;2;" + itoa(rgb.r()) + ";" + itoa(rgb.g()) + ";" + itoa(rgb.b()) + "m";
	return code + text + "\033[0m";
}

string bg(const string & text, color_t rgb) {
	return "\033[48;2;" + itoa(rgb.r()) + ";" +
		itoa(rgb.g()) + ";" + itoa(rgb.b()) + "m" + text + "\033[0m";
}

string themed(const string & text, const theme_t & theme) {
	string code = "\033[";
	if (theme.bold) code += "1;";
	if (theme.italic) code += "3;";
	if (theme.underline) code += "4;";
	if (theme.fg.hex != -1) {
		code += "38;2;" +
			itoa(theme.fg.r()) + ";" +
			itoa(theme.fg.g()) + ";" +
			itoa(theme.fg.b()) + ";";
	}
	if (theme.bg.hex != -1) {
		code += "48;2;" +
			itoa(theme.bg.r()) + ";" +
			itoa(theme.bg.g()) + ";" +
			itoa(theme.bg.b()) + ";";
	}
	code.pop_back(); // remove trailing ';'
	code += "m";
	return code + text + "\033[0m";
}

string themed(const string & text, const string & key) {
	if (THEME.find(key) == THEME.end()) return text;
	return themed(text, THEME[key]);
}

string themed(const string & text, const vector<string> & keys) {
	theme_t merged;
	for (const auto & key : keys)
		if (THEME.find(key) != THEME.end()) {
			const theme_t & t = THEME[key];
			if (t.fg.hex != -1) merged.fg = t.fg;
			if (t.bg.hex != -1) merged.bg = t.bg;
			merged.bold |= t.bold;
			merged.italic |= t.italic;
			merged.underline |= t.underline;
		}
	return themed(text, merged);
}

template <> color_t json::Value::as<color_t>() const {
	if (type == json::Value::Type::Number) return color_t((uint32_t)n);
	if (type == json::Value::Type::String) {
		string hex = * s;
		if (hex[0] == '#') {
			hex = hex.substr(1);
			if (hex.size() == 6) return color_t(hex2i(hex));
			else if (hex.size() == 3) {
				uint8_t r = hex2i(hex.substr(0, 1));
				uint8_t g = hex2i(hex.substr(1, 1));
				uint8_t b = hex2i(hex.substr(2, 1));
				return color_t((r << 4) | r, (g << 4) | g, (b << 4) | b);
			}
		}
	}
	throw runtime_error("value is not a color");
}

#endif
