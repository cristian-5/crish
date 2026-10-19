
#ifndef HISTORY_HPP
#define HISTORY_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h> // flock

#include "common.hpp"

using std::vector;
using std::string;
using std::cout;
using std::ifstream;

const size_t MAX_HISTORY_ENTRIES = 512; // max number of entries to keep in memory

class History {
public:

	/// Loads existing entries from disk and remembers the
	/// path so future `add()` calls persist immediately.
	void load(const string & path) {
		file_path = path;
		ifstream in(path);
		if (!in) return;
		string line;
		while (getline(in, line))
			if (!line.empty()) entries.push_back(line);
	}

	void add(const string & entry) {
		if (entry.empty()) return;
		if (!entries.empty() && entries.back() == entry) return; // skip consecutive dupes
		entries.push_back(entry);
		append_to_file(entry);
		if (entries.size() > MAX_HISTORY_ENTRIES) {
			size_t excess = entries.size() - MAX_HISTORY_ENTRIES;
			entries.erase(entries.begin(), entries.begin() + excess);
			trim_file_to_last(MAX_HISTORY_ENTRIES);
		}
	}

	void reset() {
		filtered.clear();
		pos = -1;
	}

	void up(string & buffer) {
		if (pos == -1) {
			prefix = buffer;
			saved_draft = buffer;
			filtered.clear();
			for (int i = 0; i < (int)entries.size(); i++)
				if (entries[i].rfind(prefix, 0) == 0)
					filtered.push_back(i);
			if (filtered.empty()) { ring_bell(); return; }
			pos = (int)filtered.size() - 1; // most recent match first
			buffer = entries[filtered[pos]];
			return;
		}
		if (pos == 0) { ring_bell(); return; } // already at the oldest match
		pos--;
		buffer = entries[filtered[pos]];
	}

	void down(string & buffer) {
		if (pos == -1) return; // nothing below the live buffer
		if (pos + 1 < (int)filtered.size()) {
			pos++;
			buffer = entries[filtered[pos]];
			return;
		}
		buffer = saved_draft; // past the newest match: restore what the user was typing
		reset();
	}

private:

	// Opens with O_APPEND (kernel guarantees the write always lands at
	// end-of-file, even with other processes appending concurrently) and
	// wraps the write in flock as a second layer of safety, so two crish
	// instances writing at the same instant can't interleave or clobber
	// each other's line.
	void append_to_file(const string & entry) {
		if (file_path.empty()) return;
		int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
		if (fd == -1) return;
		if (flock(fd, LOCK_EX) == 0) {
			string line = entry + "\n";
			write(fd, line.c_str(), line.size());
			flock(fd, LOCK_UN);
		}
		close(fd);
	}

	void trim_file_to_last(size_t keep) {
		if (file_path.empty()) return;
		int fd = open(file_path.c_str(), O_RDWR | O_CREAT, 0600);
		if (fd == -1) return;
		if (flock(fd, LOCK_EX) != 0) { close(fd); return; }
		// read everything under the lock
		vector<string> lines;
		{
			ifstream in(file_path);
			string line;
			while (getline(in, line))
				if (!line.empty()) lines.push_back(line);
		}
		if (lines.size() > keep)
			lines.erase(lines.begin(), lines.end() - keep);
		// rewrite in place through the locked fd
		if (ftruncate(fd, 0) == 0) {
			lseek(fd, 0, SEEK_SET);
			string out;
			for (const auto & l : lines) out += l + "\n";
			write(fd, out.c_str(), out.size());
		}
		flock(fd, LOCK_UN);
		close(fd);
	}

	string file_path;
	vector<string> entries;
	vector<int> filtered; // indices into entries, oldest -> newest, matching `prefix`
	int pos = -1;         // -1 means "not navigating"
	string prefix;        // buffer text at the moment Up was first pressed
	string saved_draft;   // same text, restored when navigating back past the newest match

};

#endif
