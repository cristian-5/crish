
#ifndef COLOR_HPP
#define COLOR_HPP

#include <string>

using std::string;

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

string fg(const string & text, uint32_t rgb, bool bold = false) {
	string code = "\033[";
	if (bold) code += "1;";
	code += "38;2;" + std::to_string((rgb >> 16) & 0xFF) + ";" +
		std::to_string((rgb >> 8) & 0xFF) + ";" +
		std::to_string(rgb & 0xFF) + "m";
	return code + text + "\033[0m";
}

string bg(const string & text, uint32_t rgb) {
	return "\033[48;2;" + std::to_string((rgb >> 16) & 0xFF) + ";" +
		std::to_string((rgb >> 8) & 0xFF) + ";" +
		std::to_string(rgb & 0xFF) + "m" + text + "\033[0m";
}

#endif
