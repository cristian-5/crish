
#ifndef LEXER_HPP
#define LEXER_HPP

#include <vector>
#include <string>

using std::vector;
using std::string;

struct Token {
	string lexeme;
	bool is_space;
};

bool isboundary(char c) {
	return c == '"' || c == '\''||
		   c == '|' || c == '&' ||
		   c == '<' || c == '>' || c == ';';
}

bool isoperator(const string & s) {
	return s == "|"  || s == "&"   || s == ";" ||
		   s == "<<" || s == "<<<" ||
		   s == ">>" || s == ">>>";
}

vector<Token> tokenize(const string & input) {
	vector<Token> tokens;
	size_t i = 0;
	while (i < input.size()) {
		if (isspace((unsigned char) input[i])) {
			string sp;
			while (i < input.size() && isspace((unsigned char) input[i]))
				sp += input[i++];
			tokens.push_back({sp, true});
		}
		else if (input[i] == '"' || input[i] == '\'') {
			char q = input[i];
			string str = string(1, q);
			i++;
			while (i < input.size() && input[i] != q)
				str += input[i++];
			if (i < input.size())
				str += input[i++];
			tokens.push_back({str, false});
		} else if (input.substr(i, 3) == "<<<" || input.substr(i, 3) == ">>>") {
			tokens.push_back({input.substr(i, 3), false});
			i += 3;
		} else if (input.substr(i, 2) == "<<" || input.substr(i, 2) == ">>") {
			tokens.push_back({input.substr(i, 2), false});
			i += 2;
		} else if (input[i] == '|' || input[i] == '&' || input[i] == ';' ||
				   input[i] == '<' || input[i] == '>') {
			tokens.push_back({string(1, input[i]), false});
			i++;
		} else {
			string word;
			while (i < input.size()) {
				if (input[i] == '\\' && i + 1 < input.size()) {
					word += input[i];     // keep the backslash
					word += input[i + 1]; // keep the escaped char too
					i += 2;
					continue; // skip the terminator check below for this pair
				}
				if (isspace((unsigned char) input[i]) || isboundary(input[i])) break;
				word += input[i++];
			}
			if (word.empty()) { word += input[i++]; }
			tokens.push_back({word, false});
		}
	}
	return tokens;
}

#endif
