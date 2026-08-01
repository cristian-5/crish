
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <filesystem>
#include <termios.h>
#include <thread>
#include <fcntl.h>
#include <cstdlib>
#include <algorithm>

#include "color.hpp"
#include "common.hpp"
#include "lexer.hpp"
#include "completion.hpp"

using namespace std;
namespace fs = filesystem;

enum Theme : uint32_t {
	THEME_FAILURE  = 0xFF0000, // red
	THEME_SUCCESS  = 0x00FF00, // green
	THEME_TIME     = 0xfe5f00, // orange
	THEME_INTERVAL = 0xf5f543, // yellow
	THEME_USER     = 0xfe5fd7, // pink
	THEME_CWD      = 0x36f6f7, // cyan
	THEME_DEVICE   = 0x1893ff, // blue
	THEME_COMMAND  = 0xFFFFFF, // white
	THEME_OPTION   = 0xFFFF00, // yellow
	THEME_NUMBER   = 0x00FFFF, // cyan
	THEME_STRING   = 0x0aad98, // aqua
	THEME_FILE     = 0xc966c9, // pink
	THEME_EXEC     = 0x00FF00, // green
	THEME_ERROR    = 0xFF0000  // red
};

bool is_executable_in_path(const string & cmd) {
	string resolved = expand_tilde(cmd);
	if (resolved.find('/') != string::npos)
		return fs::exists(resolved) && (fs::status(resolved).permissions() & fs::perms::owner_exec) != fs::perms::none;
	const char * path_env = getenv("PATH");
	if (!path_env) return false;
	stringstream ss(path_env);
	string dir;
	while (getline(ss, dir, ':')) {
		string full_path = dir + "/" + cmd;
		if (fs::exists(full_path) && (fs::status(full_path).permissions() & fs::perms::owner_exec) != fs::perms::none)
			return true;
	}
	return false;
}

string highlight_syntax(const string &input) {
	auto tokens = tokenize(input);
	string res;
	bool expect_cmd = true;
	for (const auto &tok : tokens) {
		if (tok.is_space) {
			res += tok.lexeme;
			continue;
		}
		string word = tok.lexeme;
		if (word == "|" || word == "&" ||
			word == ">>" || word == ">>>" ||
			word == "<<" || word == "<<<") {
			res += word;
			if (word == "|")
				expect_cmd = true;
		} else if (expect_cmd) {
			if (is_executable_in_path(word))
				res += fg(word, THEME_EXEC);
			else
				res += fg(word, THEME_FAILURE); // red if command doesn't exist
			expect_cmd = false;
		}
		else if (word.size() > 1 && word[0] == '-' && (isalpha(word[1]) || word[1] == '-'))
			res += fg(word, THEME_OPTION);
		else if ((word.size() > 1 && word[0] == '-' && isdigit(word[1])) || isdigit(word[0]))
			res += fg(word, THEME_NUMBER);
		else if (word[0] == '"' || word[0] == '\'') res += fg(word, THEME_STRING);
		else if (fs::exists(expand_tilde(word))) res += fg(word, THEME_FILE);
		else res += fg(word, THEME_COMMAND); // subcommands
	}
	return res;
}

void redraw_prompt(const string & duration_note) {
	auto now = chrono::system_clock::now();
	time_t now_c = chrono::system_clock::to_time_t(now);
	tm local_tm = * localtime(& now_c);
	char buffer[10]; // HH:MM:SS\0 (8 chars + null terminator)
	strftime(buffer, sizeof(buffer), "%H:%M:%S", & local_tm);
	string time(buffer);
	struct passwd * pw = getpwuid(geteuid());
	string username = get_username(pw), cwd = get_formatted_cwd(pw);
	string os = get_os_version_string();
	cout << " " << fg(time, THEME_TIME, BOLD)
		 << " with " << fg(username, THEME_USER, BOLD)
		 << " in " << fg(cwd, THEME_CWD, BOLD)
		 << " on " << fg(os, THEME_DEVICE, BOLD);
	if (!duration_note.empty())
		cout << " took " << fg(duration_note, THEME_INTERVAL, BOLD);
	cout << endl;
}

void redraw_line(const string & args) {
	// \r moves cursor to column 0; \033[K clears the line from cursor onwards
	cout << "\r\033[K" << fg("% ", THEME_SUCCESS) << highlight_syntax(args) << flush;
}

string read_input_raw(const string & duration_note) {
	enable_raw_mode();
	string buffer;
	char c;
	redraw_prompt(duration_note);
	redraw_line(buffer);
	while (read(STDIN_FILENO, &c, 1) == 1) {
		if (c == '\n') {
			cout << "\n";
			break;
		} else if (c == 127 || c == 8) { // backspace or DEL
			if (!buffer.empty()) buffer.pop_back();
		}
		else if (c == '\t') handle_tab_completion(buffer);
		else if (c >= 32 && c <= 126) buffer += c;
		redraw_line(buffer);
	}
	disable_raw_mode();
	return buffer;
}

struct ParsedCommand {
	vector<string> args;
	string in_file;		// << file
	string here_string; // <<< string
	string out_file;	// >> file
	string err_file;	// >>> file
};

bool handle_builtin(const vector<string> & args) {
	if (args.empty()) return true;
	if (args[0] == "cd") {
		if (args.size() < 2) cerr << fg("cd: missing argument", THEME_ERROR) << endl;
		else if (chdir(expand_tilde(args[1]).c_str()) != 0) perror("cd failed");
		return true;
	} else if (args[0] == "sleep") {
		if (args.size() < 2) cerr << fg("sleep: missing argument", THEME_ERROR) << endl;
		else try {
			double seconds = stod(args[1]);
			if (seconds > 0)
				this_thread::sleep_for(chrono::duration<double>(seconds));
		} catch (...) {
			cerr << fg("sleep: invalid argument", THEME_ERROR) << endl;
		}
		return true;
	} else if (args[0] == "clean" || args[0] == "clear") {
		cout << "\033[3J" << flush;
		return true;
	}
	return false;
}

void execute_pipeline(const string & input) {
	auto tokens = tokenize(input);
	if (tokens.empty()) return;
	vector<ParsedCommand> cmds(1);
	bool background = false;
	for (size_t i = 0; i < tokens.size(); ++i) {
		if (tokens[i].is_space) continue;
		string t = tokens[i].lexeme;
		if (t == "|") cmds.push_back(ParsedCommand());
		else if (t == "&") background = true;
		else if (t == "<<" && i+1 < tokens.size()) {
			while (++i < tokens.size() && tokens[i].is_space);
			if (i < tokens.size()) cmds.back().in_file = tokens[i].lexeme;
		} else if (t == "<<<") {
			while (++i < tokens.size() && tokens[i].is_space);
			if (i < tokens.size()) cmds.back().here_string = tokens[i].lexeme;
		} else if (t == ">>") {
			while (++i < tokens.size() && tokens[i].is_space);
			if (i < tokens.size()) cmds.back().out_file = tokens[i].lexeme;
		} else if (t == ">>>") {
			while (++i < tokens.size() && tokens[i].is_space);
			if (i < tokens.size()) cmds.back().err_file = tokens[i].lexeme;
		} else {
			if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front())
				t = t.substr(1, t.size() - 2);
			t = expand_tilde(t);
			cmds.back().args.push_back(t);
		}
	}
	if (cmds[0].args.empty()) return;
	if (cmds.size() == 1 && handle_builtin(cmds[0].args)) return;
	int num_cmds = cmds.size(), num_pipes = max(0, num_cmds - 1);
	vector<int> pipefds(2 * num_pipes); 
	for (int i = 0; i < num_pipes; i++) pipe(pipefds.data() + i * 2);
	vector<pid_t> pids;
	for (int i = 0; i < num_cmds; i++) {
		pid_t pid = fork();
		if (pid == 0) {
			if (i > 0) dup2(pipefds[(i - 1) * 2], STDIN_FILENO); // read from previous pipe
			if (i < num_cmds - 1) dup2(pipefds[i * 2 + 1], STDOUT_FILENO); // write to next pipe
			for (int j = 0; j < 2 * num_pipes; j++) close(pipefds[j]);
			if (!cmds[i].out_file.empty()) {
				cmds[i].out_file = expand_tilde(cmds[i].out_file);
				int fd = open(cmds[i].out_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
				dup2(fd, STDOUT_FILENO); close(fd);
			}
			if (!cmds[i].err_file.empty()) {
				cmds[i].err_file = expand_tilde(cmds[i].err_file);
				int fd = open(cmds[i].err_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
				dup2(fd, STDERR_FILENO); close(fd);
			}
			if (!cmds[i].in_file.empty()) {
				cmds[i].in_file = expand_tilde(cmds[i].in_file);
				int fd = open(cmds[i].in_file.c_str(), O_RDONLY);
				if(fd != -1) { dup2(fd, STDIN_FILENO); close(fd); }
			}
			if (!cmds[i].here_string.empty()) {
				int p[2]; pipe(p);
				write(p[1], cmds[i].here_string.c_str(), cmds[i].here_string.length());
				write(p[1], "\n", 1);
				close(p[1]);
				dup2(p[0], STDIN_FILENO); close(p[0]);
			}
			vector<char *> c_args;
			for (auto & arg : cmds[i].args) c_args.push_back(const_cast<char *>(arg.c_str()));
			c_args.push_back(nullptr);
			execvp(c_args[0], c_args.data());
			perror(fg("Execution failed", THEME_ERROR).c_str());
			exit(EXIT_FAILURE);
		}
		pids.push_back(pid);
	}
	for (int i = 0; i < 2 * num_pipes; i++) close(pipefds[i]);
	if (!background)
		for (pid_t pid : pids) waitpid(pid, nullptr, 0);
}

int main(int argc, char * argv[]) {
	if (argc > 1) {
		string cmd;
		for (int i = 1; i < argc; ++i) {
			if (i > 1) cmd.push_back(' ');
			cmd += argv[i];
		}
		system(cmd.c_str());
		return EXIT_SUCCESS;
	}
	string duration_note;
	while (true) {
		string input = read_input_raw(duration_note);
		if (input == "exit") break;
		auto start = chrono::steady_clock::now();
		execute_pipeline(input);
		auto end = chrono::steady_clock::now();
		auto secs = chrono::duration_cast<chrono::seconds>(end - start).count();
		duration_note = (secs >= 1) ? format_duration(secs) : "";
	}
	return EXIT_SUCCESS;
}
