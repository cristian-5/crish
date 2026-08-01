
#ifndef COMMON_HPP
#define COMMON_HPP

#include <cstdint>
#include <string>
#include <filesystem>
#include <termios.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>

using std::string;

namespace fs = std::filesystem;

string itoa(int64_t value) {
	if (value == 0) return "0";
	bool is_negative = value < 0;
	if (is_negative) value = -value;
	string result;
	result.reserve(20); // enough for 64-bit integer
	while (value > 0) {
		result.insert(result.begin(), '0' + (value % 10));
		value /= 10;
	}
	if (is_negative) result.insert(result.begin(), '-');
	return result;
}

termios orig_termios;

void enable_raw_mode() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, & orig_termios); }

string get_username(struct passwd * pw = nullptr) {
	if (!pw) pw = getpwuid(geteuid());
	return pw && pw -> pw_name ? string(pw -> pw_name) : "user";
}

string get_home_directory(struct passwd * pw = nullptr) {
	if (!pw) pw = getpwuid(geteuid());
	const char * home = getenv("HOME");
	if (home) return string(home);
	return pw && pw -> pw_dir ? string(pw -> pw_dir) : "";
}

string unescape_path(const string &s) {
	string out;
	for (size_t i = 0; i < s.size(); i++)
		if (s[i] == '\\' && i + 1 < s.size())
			out.push_back(s[++i]);
		else out.push_back(s[i]);
	return out;
}

string expand_tilde(const string & path) {
	string unescaped = unescape_path(path);
	if (unescaped.empty() || unescaped[0] != '~') return unescaped;
	if (unescaped.size() == 1 || unescaped[1] == '/') {
		string home = get_home_directory(getpwuid(geteuid()));
		return home + unescaped.substr(1);
	}
	size_t slash = unescaped.find('/');
	string user = unescaped.substr(1, slash == string::npos ? string::npos : slash - 1);
	struct passwd * pw = getpwnam(user.c_str());
	if (!pw) return unescaped;
	string home = pw -> pw_dir;
	return slash == string::npos ? home : home + unescaped.substr(slash);
}

string get_formatted_cwd(struct passwd * pw = nullptr) {
	if (!pw) pw = getpwuid(geteuid());
	string cwd = fs::current_path().string();
	string home = get_home_directory(pw);
	if (home.empty()) return cwd;
	if (cwd == home) return "~";
	string home_prefix = (home.back() == '/') ? home : home + "/";
	if (cwd.rfind(home_prefix, 0) == 0)
		return "~/" + cwd.substr(home_prefix.length());
	return cwd;
}

string get_os_version_string() {
	struct utsname buffer;
	if (uname(& buffer) != 0) return "Unix";
	return string(buffer.sysname) + " " + buffer.release;
}

/// Formats a duration: "2s", "2m 3s", "4h 3m".
string format_duration(uint64_t total_seconds) {
	uint64_t h = total_seconds / 3600;
	uint64_t m = (total_seconds % 3600) / 60;
	uint64_t s = total_seconds % 60;
	if (h > 0) {
		string res = itoa(h) + "h";
		if (m > 0) res += " " + itoa(m) + "m";
		if (s > 0) res += " " + itoa(s) + "s";
		return res;
	} else if (m > 0) {
		string res = itoa(m) + "m";
		if (s > 0) res += " " + itoa(s) + "s";
		return res;
	} else return itoa(s) + "s";
}

#endif
