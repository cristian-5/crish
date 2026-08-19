
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

#include "theme.hpp"
#include "common.hpp"
#include "lexer.hpp"
#include "history.hpp"
#include "settings.hpp"
#include "completion.hpp"

using namespace std;
namespace fs = filesystem;

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

string BUILTIN[] = { "cd", "la", "ls", "sleep", "clean", "clear" };
const size_t N_BUILTINS = sizeof(BUILTIN) / sizeof(BUILTIN[0]);

bool is_builtin(const vector<string> & args) {
	if (args.empty()) return true;
	for (int i = 0; i < N_BUILTINS; ++i)
		if (args[0] == BUILTIN[i]) return true;
	return false;
}

bool is_builtin(const string & cmd) {
	for (int i = 0; i < N_BUILTINS; ++i)
		if (cmd == BUILTIN[i]) return true;
	return false;
}

bool is_command_valid(const string & cmd) {
	return is_builtin(cmd) || is_executable_in_path(cmd);
}

string highlight_syntax(const string &input) {
	auto tokens = tokenize(input);
	string res;
	bool expect_cmd = true;
	for (const auto & token : tokens) {
		if (token.is_space) {
			res += token.lexeme;
			continue;
		}
		string word = token.lexeme;
		if (isoperator(word)) {
			res += word;
			expect_cmd |= is_separator(word);
		} else if (expect_cmd) {
			if (is_command_valid(word)) res += themed(word, "success");
			else res += themed(word, "failure"); // red if command doesn't exist
			expect_cmd = false;
		}
		else if (word.size() > 1 && word[0] == '-' && (isalpha(word[1]) || word[1] == '-'))
			res += themed(word, "arguments");
		else if ((word.size() > 1 && word[0] == '-' && isdigit(word[1])) || isdigit(word[0]))
			res += themed(word, "numbers");
		else if (word[0] == '"' || word[0] == '\'') res += themed(word, "strings");
		else if (fs::exists(expand_tilde(word))) res += themed(word, "paths");
		else res += word; // subcommands
	}
	return res;
}

void redraw_prompt(const string & duration_note) {
	bool first = true;
	for (const auto & node : prompt_nodes) {
		string text;
		if (!first) text += " ";
		first = false;
		prompt_info_t info;
		auto now = std::chrono::system_clock::now();
		std::time_t current_time = std::chrono::system_clock::to_time_t(now);
		std::tm * local_time = std::localtime(& current_time);
		info.hh = local_time -> tm_hour;
		info.mm = local_time -> tm_min;
		info.ss = std::min(59, local_time -> tm_sec); // can be 60
		struct passwd * pw = getpwuid(geteuid());
		info.user = get_username(pw);
		info.cwd = get_formatted_cwd(pw);
		info.kernel = get_os_version_string();
		info.duration = duration_note; // took 3s
		text += node.compute(info);
		cout << themed(text, node.theme);
	}
	cout << endl;
}

void redraw_line(const string & buffer, size_t cursor, size_t & last_cursor_row) {
	size_t width = get_terminal_width();
	const size_t prefix_len = 2; // "% " — plain visible length
	size_t total_chars = prefix_len + buffer.size();
	size_t total_rows = (total_chars - 1) / width + 1;
	size_t cursor_total = prefix_len + cursor;

	if (last_cursor_row > 0) cout << "\033[" << last_cursor_row << "A";
	cout << "\r\033[J" << themed("% ", "prompt") << highlight_syntax(buffer);

	size_t cursor_row, cursor_col;
	if (cursor == buffer.size() && cursor_total % width == 0) {
		// Cursor sits exactly at the last column, nothing printed past it —
		// terminal is in "pending wrap" state, hasn't actually advanced to
		// the next row yet. Stay on the current row rather than assume an
		// early wrap that hasn't physically happened.
		cursor_row = cursor_total / width - 1;
		cursor_col = width;
	} else {
		cursor_row = cursor_total / width;
		cursor_col = cursor_total % width + 1;
	}

	size_t end_row = total_rows - 1;
	if (end_row > cursor_row) cout << "\033[" << (end_row - cursor_row) << "A";
	cout << "\033[" << cursor_col << "G";

	last_cursor_row = cursor_row;
	cout << flush;
}

string read_input_raw(const string & duration_note, History & history) {
	enable_raw_mode();
	string buffer;
	size_t cursor = 0;
	size_t last_cursor_row = 0;
	char c;
	history.reset();
	redraw_prompt(duration_note);
	redraw_line(buffer, cursor, last_cursor_row);
	ssize_t n;
	while ((n = read(STDIN_FILENO, &c, 1)) == 1) {
		if (c == '\n') {
			cout << "\n";
			break;
		} else if (c == 127 || c == 8) {
			if (cursor > 0) {
				buffer.erase(buffer.begin() + cursor - 1);
				cursor--;
			}
			history.reset();
		} else if (c == 27) { // ESC — possible escape sequence
			char seq0, seq1;
			if (read(STDIN_FILENO, &seq0, 1) != 1 || seq0 != '[') continue;
			if (read(STDIN_FILENO, &seq1, 1) != 1) continue;
			if      (seq1 == 'A') { history.up(buffer);   cursor = buffer.size(); }
			else if (seq1 == 'B') { history.down(buffer); cursor = buffer.size(); }
			else if (seq1 == 'C') { if (cursor < buffer.size()) cursor++; }
			else if (seq1 == 'D') { if (cursor > 0) cursor--; }
			else if (isdigit(seq1)) {
				string code(1, seq1);
				bool code_done = false;
				char next;
				while (read(STDIN_FILENO, &next, 1) == 1) {
					if (next == '~') break;
					if (next == ';') { code_done = true; continue; } // modifier follows
					if (isdigit(next)) { if (!code_done) code += next; continue; }
					break; // unexpected byte — bail defensively
				}
				if (code == "3" && cursor < buffer.size()) // delete key: forward-delete
					buffer.erase(buffer.begin() + cursor);
				// other codes (Home/End/PageUp/PageDown, etc.) intentionally no-ops for now
			}
		} else if (c == '\t') {
			handle_tab_completion(buffer);
			cursor = buffer.size();
		} else if (c >= 32 && c <= 126) {
			buffer.insert(buffer.begin() + cursor, c);
			cursor++;
			history.reset();
		}
		redraw_line(buffer, cursor, last_cursor_row);
	}
	disable_raw_mode();
	if (n <= 0) {
		cout << endl;
		exit(EXIT_SUCCESS);
	}
	return buffer;
}

struct ParsedCommand {
	vector<string> args;
	string in_file;		// << file
	string here_string; // <<< string
	string out_file;	// >> file
	string err_file;	// >>> file
};

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

bool handle_builtin(const vector<string> & args) {
	if (args.empty()) return true;
	if (args[0] == "cd") {
		if (args.size() < 2) cerr << themed("cd: missing argument", "failure") << endl;
		else if (chdir(expand_tilde(args[1]).c_str()) != 0) perror("cd failed");
		return true;
	} else if (args[0] == "sleep") {
		if (args.size() < 2) cerr << themed("sleep: missing argument", "failure") << endl;
		else try {
			double seconds = stod(args[1]);
			if (seconds > 0)
				this_thread::sleep_for(chrono::duration<double>(seconds));
		} catch (...) {
			cerr << themed("sleep: invalid argument", "failure") << endl;
		}
		return true;
	} else if (args[0] == "clean" || args[0] == "clear") {
		cout << "\033[2J\033[H\033[3J" << flush;
		return true;
	} else if (args[0] == "ls" || args[0] == "la") {
		bool show_hidden = (args[0] == "la");
		string target = args.size() > 1 ? expand_tilde(args[1]) : ".";
		list_directory(target, show_hidden);
		return true;
	}
	return false;
}

void execute_pipeline(const vector<Token> & tokens) {
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
	if (cmds.size() == 1 && is_builtin(cmds[0].args)) {
		if (background) {
			pid_t pid = fork();
			if (pid == 0) {
				handle_builtin(cmds[0].args);
				exit(EXIT_SUCCESS);
			}
			return;
		}
		handle_builtin(cmds[0].args);
		return;
	}
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
			perror(themed("execution failed", "failure").c_str());
			exit(EXIT_FAILURE);
		}
		pids.push_back(pid);
	}
	for (int i = 0; i < 2 * num_pipes; i++) close(pipefds[i]);
	if (!background)
		for (pid_t pid : pids) waitpid(pid, nullptr, 0);
}

void execute_line(const string & input) {
	auto tokens = tokenize(input);
	if (tokens.empty()) return;
	vector<Token> segment;
	for (const auto & token : tokens)
		if (!token.is_space && token.lexeme == ";") {
			execute_pipeline(segment);
			segment.clear();
		} else segment.push_back(token);
	execute_pipeline(segment); // final segment, no trailing ';'
}

int main(int argc, char * argv[]) {
	load_settings(expand_tilde("~/crish.json")); // loads "path" and "theme"
	if (argc > 1) {
		if (string(argv[1]) == "-c" && argc > 2) {
			string cmd;
			for (int i = 2; i < argc; ++i) {
				if (i > 2) cmd.push_back(' ');
				cmd += argv[i];
			}
			execute_line(cmd);
			return EXIT_SUCCESS;
		} else { // script file passed as argument:
			ifstream script(argv[1]);
			if (!script.is_open()) {
				cerr << "crish: could not open script file \"" << argv[1] << "\"" << endl;
				cerr << "reason: " << strerror(errno) << endl;
				return EXIT_SUCCESS;
			}
			string line;
			while (getline(script, line)) {
				if (line.empty() || line[0] == '#') continue;
				execute_line(line);
			}
			return EXIT_SUCCESS;
		}
	}
	History history;
	history.load(expand_tilde("~/.crish_history"));
	string duration_note;
	while (true) {
		string input = read_input_raw(duration_note, history);
		history.add(input);
		if (input == "exit") break;
		auto start = chrono::steady_clock::now();
		execute_line(input);
		auto end = chrono::steady_clock::now();
		auto secs = chrono::duration_cast<chrono::seconds>(end - start).count();
		duration_note = (secs >= 1) ? format_duration(secs) : "";
	}
	return EXIT_SUCCESS;
}
