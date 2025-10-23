#ifndef MINISHELL_TOKENIZE_HPP
#define MINISHELL_TOKENIZE_HPP

#include <string>
#include <vector>
#include <cctype>

namespace mshell {

// A simple token structure
struct Token {
    std::string text;
};

// ───────────────────────────────────────────────────────────────────────
// Tokenizer: split input into words while respecting quotes and escapes
// ───────────────────────────────────────────────────────────────────────
inline std::vector<Token> tokenize(const std::string& line) {
    std::vector<Token> out;
    std::string cur;
    enum State { BASE, IN_SQ, IN_DQ } st = BASE;

    auto flush = [&]() {
        if (!cur.empty()) {
            out.push_back({cur});
            cur.clear();
        }
    };

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        switch (st) {
            case BASE:
                if (std::isspace(static_cast<unsigned char>(c))) {
                    flush();
                } else if (c == '\'') {
                    st = IN_SQ;
                } else if (c == '"') {
                    st = IN_DQ;
                } else if (c == '\\') {
                    if (i + 1 < line.size()) cur.push_back(line[++i]);
                } else {
                    cur.push_back(c);
                }
                break;

            case IN_SQ:
                if (c == '\'') {
                    st = BASE;
                } else {
                    cur.push_back(c);
                }
                break;

            case IN_DQ:
                if (c == '"') {
                    st = BASE;
                } else if (c == '\\' && i + 1 < line.size()) {
                    // Handle escaped quotes and dollar signs in double quotes
                    char n = line[i + 1];
                    if (n == '"' || n == '\\' || n == '$' || n == '`') {
                        cur.push_back(n);
                        ++i;
                    } else {
                        cur.push_back('\\');
                    }
                } else {
                    cur.push_back(c);
                }
                break;
        }
    }

    flush();
    return out;
}

}

#endif // MINISHELL_TOKENIZE_HPP
