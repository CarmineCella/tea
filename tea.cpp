// tea.cpp
#include "tea.h"
#include <fstream>

int main(int argc, char* argv[]) {
    tea::OpTable ops;
    tea::init(ops);
    tea::Env env;

    if (argc == 1) {
        tea::repl(env, ops);
        return 0;
    }

    // All files run in order in the same environment; stop on the first error.
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i]);
        if (!file) {
            std::cerr << argv[i] << ": cannot open file\n";
            return 1;
        }
        std::string line;
        std::size_t lineno = 0;
        while (std::getline(file, line)) {
            ++lineno;
            try {
                tea::print(tea::eval(tea::read(line, ops), env), std::cout) << '\n';
            } catch (const std::exception& e) {
                std::cerr << argv[i] << ':' << lineno << ": " << e.what()
                          << "\n  " << line << '\n';
                return 1;
            }
        }
        if (file.bad() || (file.fail() && !file.eof())) {
            std::cerr << argv[i] << ": read error\n";
            return 1;
        }
    }
    return 0;
}
