
#ifndef LS_HPP
#define LS_HPP

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <system_error>

using std::string;
using std::vector;
using std::cerr;
using std::cout;
using std::endl;
using std::error_code;
namespace fs = std::filesystem;

#include "common.hpp"
#include "theme.hpp"

void list_directory(const string & path, bool show_hidden) {
	error_code ec;
	if (!fs::is_directory(path, ec) || ec) {
		cerr << themed("ls: not a directory: " + path, "failure") << endl;
		return;
	}
	vector<fs::directory_entry> dirs, files;
	fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
	if (ec) { cerr << themed("ls: cannot open " + path, "failure") << endl; return; }
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
	auto by_name = [](const fs::directory_entry & a, const fs::directory_entry & b) {
		return a.path().filename().string() < b.path().filename().string();
	};
	sort(dirs.begin(), dirs.end(), by_name);
	sort(files.begin(), files.end(), by_name);
	for (auto & e : dirs) {
		string name = e.path().filename().string();
		bool hidden = (!name.empty() && name[0] == '.') || has_hidden_flag(e.path());
		cout << themed(name, hidden ? "hidden-folders" : "folders") << "\n";
	}
	for (auto & e : files) {
		string name = e.path().filename().string();
		bool hidden = (!name.empty() && name[0] == '.') || has_hidden_flag(e.path());
		cout << themed(name, hidden ? "hidden-files" : "files") << "\n";
	}
}

#endif
