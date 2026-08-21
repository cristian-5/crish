#ifndef LS_HPP
#define LS_HPP

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>

#include "common.hpp"
#include "theme.hpp"

using std::string;
using std::vector;
using std::cerr;
using std::cout;
using std::endl;
using std::error_code;

namespace fs = std::filesystem;

static bool is_macos_alias_file(const fs::path & p) {
	std::error_code ec;
	auto size = fs::file_size(p, ec);
	if (ec || size == 0 || size > 20000) return false;
	std::ifstream file(p, std::ios::binary);
	if (!file) return false;
	char header[12];
	if (!file.read(header, sizeof(header))) return false;
	bool is_modern_alias = (std::memcmp(header, "book", 4) == 0) && 
						   (std::memcmp(header + 8, "mark", 4) == 0);
	bool is_legacy_alias = (std::memcmp(header, "alis", 4) == 0);
	return is_modern_alias || is_legacy_alias;
}

static bool is_symlink_entry(const fs::directory_entry & e) {
	error_code sec;
	bool is_link = e.is_symlink(sec);
	if (!sec && is_link) return true;
	return e.is_regular_file(sec) && is_macos_alias_file(e.path());
}

static bool by_name(const fs::directory_entry & a, const fs::directory_entry & b) {
	return a.path().filename().string() < b.path().filename().string();
}

/// Generates POSIX permissions string (e.g., drwxr-xr-x).
static string get_permissions_string(const fs::directory_entry& e) {
	error_code ec;
	auto status = e.status(ec);
	if (ec) return "----------";
	string p = "----------";
	if (e.is_directory(ec)) p[0] = 'd';
	else if (e.is_symlink(ec) || is_macos_alias_file(e.path())) p[0] = 'l';
	else if (e.is_block_file(ec)) p[0] = 'b';
	else if (e.is_character_file(ec)) p[0] = 'c';
	else if (e.is_fifo(ec)) p[0] = 'p';
	else if (e.is_socket(ec)) p[0] = 's';
	auto perms = status.permissions();
	auto check = [&](fs::perms bit, size_t pos, char c) {
		if ((perms & bit) != fs::perms::none) p[pos] = c;
	};
	check(fs::perms::owner_read,   1, 'r');
	check(fs::perms::owner_write,  2, 'w');
	check(fs::perms::owner_exec,   3, 'x');
	check(fs::perms::group_read,   4, 'r');
	check(fs::perms::group_write,  5, 'w');
	check(fs::perms::group_exec,   6, 'x');
	check(fs::perms::others_read,  7, 'r');
	check(fs::perms::others_write, 8, 'w');
	check(fs::perms::others_exec,  9, 'x');
	return p;
}

static string permissions_string_themed(const string & perms) {
	string res;
	for (size_t i = 0; i < perms.size(); ++i) { // always skip file type
		switch (perms[i]) {
			case 'r': res += themed("r", "permissions-r"); break;
			case 'w': res += themed("w", "permissions-w"); break;
			case 'x': res += themed("x", "permissions-x"); break;
			case '-': res += themed("-", "permissions-n"); break;
			default:  res += perms[i]; break;
		}
	}
	return res;
}

/// Formats size into max 3 digits + unit (e.g. 12B, 1.5K, 344MB, 4.2GB).
static string format_size(uintmax_t bytes) {
	if (bytes == 0) return "-";
	if (bytes < 1024) return std::to_string(bytes) + "B";
	static const char * units[] = {"KB", "MB", "GB", "TB"};
	double val = static_cast<double>(bytes) / 1024.0;
	size_t unit_idx = 0;
	while (val >= 1000.0 && unit_idx < 3) {
		val /= 1024.0;
		unit_idx++;
	}
	std::ostringstream oss;
	if (val < 10.0) oss << std::fixed << std::setprecision(1) << val; // e.g. 1.5K
	else oss << static_cast<int>(val); // e.g. 344MB
	oss << units[unit_idx];
	return oss.str();
}

/// Formats modification time to YYYY-MM-DD HH:MM.
static string format_mod_time(const fs::directory_entry& e) {
	error_code ec;
	auto ftime = e.last_write_time(ec);
	if (ec) return "1970-01-01 00:00";
	auto s_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
	);
	std::time_t tt = std::chrono::system_clock::to_time_t(s_time);
	std::tm tm_buf;
#if defined(_WIN32)
	localtime_s(&tm_buf, &tt);
#else
	localtime_r(&tt, &tm_buf);
#endif
	std::ostringstream oss;
	oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M");
	return oss.str();
}

static void print_entry(const fs::directory_entry & e, const string & dir_theme, bool long_format) {
	string name = e.path().filename().string();
	if (long_format) {
		error_code ec;
		uintmax_t sz = e.is_regular_file(ec) ? e.file_size(ec) : 0;
		string perms = permissions_string_themed(get_permissions_string(e));
		string size_str = format_size(sz);
		string time_str = format_mod_time(e);
		cout << perms << " " 
			 << std::setw(5) << std::right << size_str << " " 
			 << time_str << " ";
	}
	if (is_symlink_entry(e)) cout << themed(name, {dir_theme, "symlinks"}) << "\n";
	else cout << themed(name, dir_theme) << "\n";
}

void list_directory(const string & path, bool long_format) {
	error_code ec;
	if (!fs::is_directory(path, ec) || ec) {
		cerr << themed("ls: not a directory: " + path, "failure") << endl;
		return;
	}
	vector<fs::directory_entry> dirs, files;
	fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
	if (ec) {
		cerr << themed("ls: cannot open " + path, "failure") << endl;
		return;
	}
	for (; it != fs::directory_iterator(); it.increment(ec)) {
		if (ec) break;
		string name = it -> path().filename().string();
		bool hidden = (!name.empty() && name[0] == '.') || has_hidden_flag(it -> path());
		if (hidden && !long_format) continue;
		error_code stat_ec;
		bool is_dir = it -> is_directory(stat_ec);
		if (stat_ec) continue; // broken symlink etc — skip just this entry
		if (is_dir) dirs.push_back(*it);
		else files.push_back(*it);
	}
	sort(dirs.begin(), dirs.end(), by_name);
	sort(files.begin(), files.end(), by_name);
	for (auto & e : dirs) {
		string name = e.path().filename().string();
		bool hidden = (!name.empty() && name[0] == '.') || has_hidden_flag(e.path());
		string dir_theme = hidden ? "hidden-folders" : "folders";
		print_entry(e, dir_theme, long_format);
	}
	for (auto & e : files) {
		string name = e.path().filename().string();
		bool hidden = (!name.empty() && name[0] == '.') || has_hidden_flag(e.path());
		string file_theme = hidden ? "hidden-files" : "files";
		print_entry(e, file_theme, long_format);
	}
}

#endif
