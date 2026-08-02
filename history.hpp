
#ifndef HISTORY_HPP
#define HISTORY_HPP

#include <iostream>
#include <string>
#include <vector>

#include "common.hpp"

using std::vector;
using std::string;
using std::cout;

class History {
public:

	void add(const string & entry) {
		if (!entry.empty() && (entries.empty() || entries.back() != entry))
			entries.push_back(entry);
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

	vector<string> entries;
	vector<int> filtered; // indices into entries, oldest -> newest, matching `prefix`
	int pos = -1;         // -1 means "not navigating"
	string prefix;        // buffer text at the moment Up was first pressed
	string saved_draft;   // same text, restored when navigating back past the newest match

};

#endif
