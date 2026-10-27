// tea.cpp
//

#include "tea.h"

int main(int argc, char* argv[]) {
    tea::OpTable ops;
    tea::init(ops);
    tea::Env env;

    tea::repl(env, ops);
    return 0;
}

// eof
