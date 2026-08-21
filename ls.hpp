
#ifndef LS_HPP
#define LS_HPP

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <system_error>

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

static bool is_symlink_entry(const fs::directory_entry& e) {
    error_code sec;
    bool is_link = e.is_symlink(sec);
    if (!sec && is_link) return true;
    return e.is_regular_file(sec) && is_macos_alias_file(e.path());
}

static bool by_name(const fs::directory_entry & a, const fs::directory_entry & b) {
	return a.path().filename().string() < b.path().filename().string();
}

void list_directory(const string & path, bool show_hidden) {
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
		if (hidden && !show_hidden) continue;
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
		if (is_symlink_entry(e)) cout << themed(name, {dir_theme, "symlinks"}) << "\n";
		else cout << themed(name, dir_theme) << "\n";
	}
	for (auto & e : files) {
		string name = e.path().filename().string();
		bool hidden = (!name.empty() && name[0] == '.') || has_hidden_flag(e.path());
		string file_theme = hidden ? "hidden-files" : "files";
		if (is_symlink_entry(e)) cout << themed(name, {file_theme, "symlinks"}) << "\n";
		else cout << themed(name, file_theme) << "\n";
	}
}

#endif
