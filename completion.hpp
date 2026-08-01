
#ifndef COMPLETION_HPP
#define COMPLETION_HPP

#include <vector>
#include <string>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <system_error>

#include "common.hpp"
#include "lexer.hpp"

using std::vector;
using std::string;
using std::stringstream;
using std::error_code;

namespace fs = std::filesystem;

struct CompletionContext {
	string prefix;
	bool is_cmd;
};

CompletionContext analyze_completion_context(const vector<Token> & tokens) {
	CompletionContext ctx { "", true };
	if (!tokens.empty() && !tokens.back().is_space) {
		ctx.prefix = tokens.back().lexeme;
		int idx = tokens.size() - 2;
		while (idx >= 0 && tokens[idx].is_space) idx--;
		if (idx >= 0 && tokens[idx].lexeme != "|") ctx.is_cmd = false;
	} else {
		int idx = tokens.size() - 1;
		while (idx >= 0 && tokens[idx].is_space) idx--;
		if (idx >= 0 && tokens[idx].lexeme != "|") ctx.is_cmd = false;
	}
	if (!ctx.prefix.empty() && (ctx.prefix.front() == '"' || ctx.prefix.front() == '\''))
		ctx.prefix = ctx.prefix.substr(1);
	return ctx;
}

vector<string> complete_from_path(const string & prefix) {
	vector<string> matches;
	if (prefix.find('/') != string::npos) return matches;
	const char * path_env = getenv("PATH");
	if (!path_env) return matches;
	stringstream ss(path_env);
	string dir;
	while (getline(ss, dir, ':')) {
		error_code ec;
		fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
		if (ec) continue; // can't open this PATH dir — skip it, try the next one
		for (; it != fs::directory_iterator(); it.increment(ec)) {
			if (ec) break; // stop this dir on error, PATH dirs are independent so no other loss
			string filename = it  ->  path().filename().string();
			error_code stat_ec;
			auto perms = fs::status(it  ->  path(), stat_ec).permissions();
			if (!stat_ec && filename.rfind(prefix, 0) == 0 && (perms & fs::perms::owner_exec) != fs::perms::none)
				matches.push_back(filename);
		}
	}
	return matches;
}

struct SearchAnchor {
	fs::path base;   // directory to start walking from
	string prefix;   // remaining prefix, relative to base
	string restore;  // glued back onto matches for display
};

SearchAnchor resolve_search_anchor(const string &prefix) {
	if (!prefix.empty() && prefix[0] == '~') {
		size_t slash = prefix.find('/');
		string user_part = (slash == string::npos) ? prefix : prefix.substr(0, slash);
		string expanded = expand_tilde(user_part);
		if (fs::exists(expanded)) {
			string rest = (slash == string::npos) ? "" : prefix.substr(slash + 1);
			return { fs::path(expanded), rest, user_part + "/" };
		}
		return { fs::current_path(), prefix, "" }; // unknown ~user, fall back
	}
	if (!prefix.empty() && prefix[0] == '/')
		return { fs::path("/"), prefix.substr(1), "/" };
	return { fs::current_path(), prefix, "" };
}

struct TildeContext {
	fs::path base;
	string prefix;
	string restore; // glued back onto matches for display
};

TildeContext resolve_tilde(const string &prefix) {
	TildeContext ctx{fs::current_path(), prefix, ""};
	if (prefix.empty() || prefix[0] != '~') return ctx;
	size_t slash = prefix.find('/');
	string user_part = (slash == string::npos) ? prefix : prefix.substr(0, slash);
	string expanded = expand_tilde(user_part);
	if (!fs::exists(expanded)) return ctx; // unknown ~user etc — fall back to CWD search
	ctx.base = expanded;
	ctx.prefix = (slash == string::npos) ? "" : prefix.substr(slash + 1);
	ctx.restore = user_part + "/";
	return ctx;
}

vector<string> complete_from_filesystem(const fs::path & base, const string & prefix, bool is_cmd) {
	vector<string> matches, components;
	size_t start = 0;
	while (true) {
		size_t slash = prefix.find('/', start);
		if (slash == string::npos) {
			components.push_back(prefix.substr(start));
			break;
		}
		components.push_back(prefix.substr(start, slash - start));
		start = slash + 1;
	}
	fs::path dir = base;
	string rel_so_far;
	for (size_t i = 0; i + 1 < components.size(); ++i) {
		fs::path next = dir / components[i];
		error_code ec;
		if (!fs::is_directory(next, ec) || ec) return matches; // typed path doesn't exist
		dir = next;
		rel_so_far += components[i] + "/";
	}
	const string & final_prefix = components.back();
	error_code ec;
	fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
	if (ec) return matches; // can't open this exact directory
	for (; it != fs::directory_iterator(); it.increment(ec)) {
		if (ec) break;
		string name = it -> path().filename().string();
		if (name.rfind(final_prefix, 0) != 0) continue;
		if (final_prefix.empty() && !name.empty() && name[0] == '.') continue; // don't surface dotfiles unasked
		error_code stat_ec;
		bool is_dir = it -> is_directory(stat_ec);
		if (stat_ec) continue; // broken symlink etc — skip just this entry
		if (is_cmd && !is_dir) {
			auto perms = fs::status(it -> path(), stat_ec).permissions();
			if (stat_ec || (perms & fs::perms::owner_exec) == fs::perms::none) continue;
		}
		matches.push_back(rel_so_far + name);
	}
	return matches;
}

void apply_best_match(string & buffer, const vector<Token> & tokens, vector<string> matches) {
	if (matches.empty()) return;
	sort(matches.begin(), matches.end());
	matches.erase(unique(matches.begin(), matches.end()), matches.end());
	const string &completion = matches[0];
	if (tokens.empty() || tokens.back().is_space) buffer += completion;
	else {
		buffer.erase(buffer.size() - tokens.back().lexeme.size());
		buffer += completion;
	}
}

void handle_tab_completion(string & buffer) {
	auto tokens = tokenize(buffer);
	CompletionContext ctx = analyze_completion_context(tokens);
	TildeContext tc = resolve_tilde(ctx.prefix);
	vector<string> matches = complete_from_filesystem(tc.base, tc.prefix, ctx.is_cmd);
	for (auto & m : matches) {
		if (!tc.restore.empty()) m = tc.restore + m;
		if (m.find(' ') != string::npos) m = "\"" + m + "\"";
	}
	SearchAnchor anchor = resolve_search_anchor(ctx.prefix);
	auto fs_matches = complete_from_filesystem(anchor.base, anchor.prefix, ctx.is_cmd);
	for (auto & m : fs_matches) {
		m = anchor.restore + m;
		if (m.find(' ') != string::npos) m = "\"" + m + "\"";
	}
	matches.insert(matches.end(), fs_matches.begin(), fs_matches.end());
	apply_best_match(buffer, tokens, matches);
}

#endif
