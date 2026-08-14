
#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <format>
#include <filesystem>
#include <system_error>

#include "common.hpp"
#include "theme.hpp"
#include "json.hpp"

using std::string;
using std::ifstream;
using std::stringstream;
using std::runtime_error;
using std::error_code;
using std::cerr;
using std::endl;

namespace fs = std::filesystem;

using json::JSON;
using json::Value;

template <class TYPE>
void grab(const string & key, Value * object, TYPE & into) {
	if (!object || object -> type != Value::Type::Object) return;
	Value * value = object -> operator[](key);
	try { if (value) into = value -> as<TYPE>(); }
	catch (const runtime_error & e) {
		cerr << themed("error parsing settings key \"" + key + "\": "
			+ string(e.what()), THEME["failure"]) << endl;
	}
}

struct prompt_info_t {
	int hh, mm, ss;
	string user, cwd, kernel, duration;
};

class prompt_node_t {
public:
	string text;
	theme_t theme;
	string enabler;
	prompt_node_t(const string & t) : text(t) { }
	prompt_node_t(const string & t, const theme_t & th) : text(t), theme(th) { }
	prompt_node_t(const string & t, const theme_t & th, const string & e) : text(t), theme(th), enabler(e) { }
	string compute(const prompt_info_t & info) const {
		if (!enabled(info)) return "";
		string result = text;
		replace(result, "$HH", std::format("{:02}", info.hh));
		replace(result, "$MM", std::format("{:02}", info.mm));
		replace(result, "$SS", std::format("{:02}", info.ss));
		replace(result, "$USER", info.user);
		replace(result, "$CWD", info.cwd);
		replace(result, "$KERNEL", info.kernel);
		replace(result, "$DURATION", info.duration);
		return result;
	}
private:
	bool enabled(const prompt_info_t & info) const {
		if (enabler.empty()) return true;
		if (enabler == "$DURATION") return !info.duration.empty();
		if (enabler == "$HH") return info.hh > 0;
		if (enabler == "$MM") return info.mm > 0;
		if (enabler == "$SS") return info.ss > 0;
		return false;
	}
};

vector<prompt_node_t> prompt_nodes;
string prompt_symbol = "%";

void load_settings(const string & filename) {
	ifstream file(filename);
	stringstream buffer;
	buffer << file.rdbuf();
	string content = buffer.str();
    Value * settings = nullptr;
	try { settings = JSON::parse(content); }
	catch (const runtime_error & e) {
		string message = string(e.what());
		cerr << themed("error parsing settings file: " + message, "failure") << endl;
		return;
	}
	if (!settings || settings -> type != Value::Type::Object) {
		cerr << themed("settings file must contain a JSON object", "failure") << endl;
		return;
	}
	// load "path" if available:
	Value * path = (* settings)["path"];
	if (path && path -> type == Value::Type::Array) {
		string joined;
		for (size_t i = 0; i < path -> a -> size(); ++i) {
			Value * entry = (* path)[i];
			if (entry && entry -> type == Value::Type::String) {
				string dir = expand_tilde(entry -> as<string>());
				error_code ec;
				if (!fs::is_directory(dir, ec) || ec) continue; // skip typos / missing dirs
				if (!joined.empty()) joined += ":";
				joined += dir;
			}
		}
		if (!joined.empty()) setenv("PATH", joined.c_str(), 1);
	}
	// load "theme" if available:
	Value * theme = (* settings)["theme"];
	if (theme && theme -> type == Value::Type::Object)
		for (const auto & [key, object] : *(theme -> o)) {
			if (object && object -> type != Value::Type::Object) continue;
			theme_t t;
			grab<color_t>("fg", object, t.fg);
			grab<color_t>("bg", object, t.bg);
			grab<bool>("bold", object, t.bold);
			grab<bool>("italic", object, t.italic);
			grab<bool>("underline", object, t.underline);
			THEME[key] = t;
		}
	// load "prompt" if available:
	Value * prompt = (* settings)["prompt"];
	if (prompt && prompt -> type == Value::Type::Object) {
		grab<string>("symbol", prompt, prompt_symbol);
		Value * components = (* prompt)["components"];
		if (components && components -> type == Value::Type::Array) {
			for (const auto & item : *(components -> a)) {
				if (item && item -> type == Value::Type::String) {
					prompt_nodes.push_back(prompt_node_t(item -> as<string>()));
					continue;
				}
				if (item && item -> type != Value::Type::Object) continue;
				string text, enabler;
				theme_t t;
				grab<string>("text", item, text);
				grab<color_t>("fg", item, t.fg);
				grab<color_t>("bg", item, t.bg);
				grab<bool>("bold", item, t.bold);
				grab<bool>("italic", item, t.italic);
				grab<bool>("underline", item, t.underline);
				grab<string>("enabler", item, enabler);
				prompt_nodes.push_back(prompt_node_t(text, t, enabler));
			}
		}
	}
	delete settings;
}

#endif
