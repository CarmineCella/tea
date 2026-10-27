// tea.h
#ifndef TEA_H
#define TEA_H
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>
namespace tea {
using std::size_t;

// ast
using Array = std::valarray<double>;
using ArrayPtr = std::shared_ptr<const Array>;
using List = std::vector<ArrayPtr>;
using Action = List (*)(const List&);
struct Opcode {
    Action fn;
    size_t min;
    int max;
    size_t outputs;
};
using Token = std::variant<std::string, ArrayPtr, Opcode>;
using Command = std::vector<Token>;
using Env = std::map<std::string, ArrayPtr>;
using OpTable = std::map<std::string, Opcode>;

// helpers
inline const Array& array(const ArrayPtr& p) {
    if (!p) throw std::runtime_error("invalid value");
    return *p;
}
inline std::ostream& print(const List& n, std::ostream& out) {
    for (size_t i = 0; i < n.size(); ++i) {
        const Array& a = array(n[i]);
        out << "[";
        for (size_t j = 0; j < a.size(); ++j) {
            out << (j ? " " : "") << a[j];
        }
        out << "]";
    }
    return out;
}

// reader
inline bool parse_number(const std::string& s, double& value) {
    std::istringstream in(s);
    return bool(in >> value) && in.eof();
}
inline Command read(const std::string& line, const OpTable& ops) {
    std::stringstream lstream(line);
    Command command;
    for (std::string token; lstream >> token;) {
        double val = 0;
        if (parse_number(token, val)) command.emplace_back(std::make_shared<const Array>(Array{val}));
        else {
            auto p = ops.find(token);
            if (p != ops.end()) command.emplace_back(p->second);
            else command.emplace_back(token);
        }
    }
    return command;
}

// evaluator
inline ArrayPtr resolve(const Token& token, const Env& env) {
    ArrayPtr value;
    if (const auto* p = std::get_if<ArrayPtr>(&token)) value = *p;
    else {
        const auto* name = std::get_if<std::string>(&token);
        if (!name) throw std::runtime_error("expected value or variable");
        auto found = env.find(*name);
        if (found == env.end()) throw std::runtime_error("invalid identifier " + *name);
        value = found->second;
    }
    if (!value) throw std::runtime_error("invalid value");
    return value;
}
inline List eval(const Command& command, Env& env) {
    // out1 out2 ... opcode in1 in2 ...
    std::vector<std::string> outputs;
    if (command.size() == 1 && !std::holds_alternative<Opcode>(command[0])) return {resolve(command[0], env)};
    for (size_t i = 0; i < command.size(); ++i) {
        if (const auto* name = std::get_if<std::string>(&command[i])) {
            outputs.push_back(*name);
            continue;
        }
        const auto* op = std::get_if<Opcode>(&command[i]);
        if (!op) throw std::runtime_error("expected symbol or opcode");
        List params;
        for (size_t k = i + 1; k < command.size(); ++k) params.push_back(resolve(command[k], env));
        if (params.size() < op->min || (op->max >= 0 && params.size() > size_t(op->max)) || (!outputs.empty() && outputs.size() != op->outputs)) {
            throw std::runtime_error("parameters mismatch");
        }
        if (!op->fn) throw std::runtime_error("invalid opcode");
        List res = op->fn(params);
        if (res.size() != op->outputs) throw std::runtime_error("outputs mismatch");
        for (const auto& value : res) if (!value) throw std::runtime_error("invalid opcode result");
        for (size_t j = 0; j < outputs.size(); ++j) env[outputs[j]] = res[j];
        return res;
    }
    throw std::runtime_error("opcode expected");
}

// opcode
inline double scalar(const ArrayPtr& p) {
    const Array& a = array(p);
    if (a.size() != 1) throw std::runtime_error("expected a scalar");
    return a[0];
}
inline const size_t LIMIT = 1 << 27; // elements per array: 1 GB of doubles
inline size_t index(double d, size_t limit, size_t wrap = 0) { // an integer in [0, limit); -k counts back from wrap
    if (d < 0) d += double(wrap);
    if (!(d >= 0 && d < double(limit) && std::trunc(d) == d)) throw std::runtime_error("index out of range");
    return size_t(d);
}
inline size_t index(const ArrayPtr& p, size_t limit, size_t wrap = 0) {
    return index(scalar(p), limit, wrap);
}
inline size_t length(const ArrayPtr& p) {
    return array(p).size();
}
inline size_t broadcast_size(size_t x, size_t y) {
    if (x == y) return x;
    if (x == 1) return y;
    if (y == 1) return x;
    throw std::runtime_error("incompatible sizes " + std::to_string(x) + " and " + std::to_string(y));
}
template<class Op, class Result> Result broadcast(const Array& x, const Array& y) {
    if (x.size() == y.size()) return Op()(x, y);
    if (x.size() == 1) return Op()(x[0], y);
    if (y.size() == 1) return Op()(x, y[0]);
    throw std::runtime_error("incompatible sizes " + std::to_string(x.size()) + " and " + std::to_string(y.size()));
}
inline List make(const List& in) {
    if (in.size() > LIMIT) throw std::runtime_error("array too large");
    Array a(in.size());
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] = scalar(in[i]);
    }
    return {std::make_shared<const Array>(std::move(a))};
}
template<class Op, int Unit> List arith(const List& in) { // r add a b c ...; r sub x is (- Unit x): 0 for + -, 1 for * /
    List out;
    if (in.size() == 1) out.push_back(std::make_shared<const Array>(broadcast<Op, Array>(Array{double(Unit)}, array(in[0]))));
    else {
        if (in.size() < 2) throw std::runtime_error("invalid number of arguments");
        Array r = broadcast<Op, Array>(array(in[0]), array(in[1]));
        for (size_t i = 2; i < in.size(); ++i) r = broadcast<Op, Array>(r, array(in[i]));
        out.push_back(std::make_shared<const Array>(std::move(r)));
    }
    return out;
}
template<double (*F)(double)> List unary(const List& a) {
    return {std::make_shared<const Array>(Array(array(a[0]).apply(F)))};
}
inline List aslice(const List& a) {
    size_t n = length(a[0]), s = index(a[1], n + 1, n), e = index(a[2], n + 1, n), step = a.size() == 4 ? index(a[3], LIMIT + 1) : 1;
    if (e < s || step == 0) throw std::runtime_error("invalid slice");
    size_t count = (e - s) / step + ((e - s) % step != 0);
    return {std::make_shared<const Array>(Array(array(a[0])[std::slice(s, count, step)]))};
}
inline List range(const List& a) {
    double start = a.size() > 1 ? scalar(a[0]) : 0;
    double end = scalar(a[a.size() > 1]);
    double step = a.size() > 2 ? scalar(a[2]) : 1;
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(step) || step == 0) throw std::runtime_error("invalid range");
    double n = std::max(std::ceil((end - start) / step), 0.0);
    if (!std::isfinite(n) || n > double(LIMIT)) throw std::runtime_error("range too large");
    Array r(static_cast<size_t>(n));
    for (size_t k = 0; k < r.size(); ++k) r[k] = std::fma(double(k), step, start);
    return {std::make_shared<const Array>(std::move(r))};
}
inline List cat(const List& a) { // r cat x y ...
    size_t n = 0;
    for (const auto& p : a) {
        size_t size = length(p);
        if (size > LIMIT - n) throw std::runtime_error("array too large");
        n += size;
    }
    Array r(n);
    size_t offset = 0;
    for (const auto& p : a) {
        const Array& x = array(p);
        r[std::slice(offset, x.size(), 1)] = x;
        offset += x.size();
    }
    return {std::make_shared<const Array>(std::move(r))};
}
inline List len(const List& a) { // r len x
    return {std::make_shared<const Array>(Array{double(length(a[0]))})};
}
inline List fill(const List& a) { // r fill count value
    size_t n = index(a[0], LIMIT + 1);
    return {std::make_shared<const Array>(Array(scalar(a[1]), n))};
}
inline List gather(const List& a) { // r gather x indices
    const Array& x = array(a[0]);
    const Array& indices = array(a[1]);
    if (indices.size() > LIMIT) throw std::runtime_error("array too large");
    Array r(indices.size());
    for (size_t i = 0; i < r.size(); ++i) r[i] = x[index(indices[i], x.size(), x.size())];
    return {std::make_shared<const Array>(std::move(r))};
}
inline List put(const List& a) { // r put x indices values; copy, last replacement wins
    const Array& x = array(a[0]);
    const Array& indices = array(a[1]);
    const Array& values = array(a[2]);
    if (x.size() > LIMIT) throw std::runtime_error("array too large");
    if (values.size() != 1 && values.size() != indices.size()) throw std::runtime_error("replacement sizes mismatch");
    for (size_t i = 0; i < indices.size(); ++i) index(indices[i], x.size(), x.size());
    Array r = x;
    for (size_t i = 0; i < indices.size(); ++i) r[index(indices[i], x.size(), x.size())] = values[values.size() == 1 ? 0 : i];
    return {std::make_shared<const Array>(std::move(r))};
}
inline List sum(const List& a) { // r sum x; empty sum is zero
    const Array& x = array(a[0]);
    return {std::make_shared<const Array>(Array{x.size() ? x.sum() : 0.0})};
}
template<class Op> List extremum(const List& a) { // r min/max x; propagate NaN
    const Array& x = array(a[0]);
    if (x.size() == 0) throw std::runtime_error("empty reduction");
    double r = x[0];
    for (size_t i = 1; i < x.size(); ++i) if (std::isnan(x[i]) || Op()(x[i], r)) r = x[i];
    return {std::make_shared<const Array>(Array{r})};
}
template<class Op> List compare(const List& a) { // r comparison x y; numeric 0/1 mask
    const Array& x = array(a[0]);
    const Array& y = array(a[1]);
    size_t n = broadcast_size(x.size(), y.size());
    if (n > LIMIT) throw std::runtime_error("array too large");
    Array r(n);
    for (size_t i = 0; i < n; ++i) r[i] = Op()(x[x.size() == 1 ? 0 : i], y[y.size() == 1 ? 0 : i]) ? 1.0 : 0.0;
    return {std::make_shared<const Array>(std::move(r))};
}
inline List where(const List& a) { // r where mask x y; nonzero mask selects x
    const Array& mask = array(a[0]);
    const Array& x = array(a[1]);
    const Array& y = array(a[2]);
    size_t n = broadcast_size(broadcast_size(mask.size(), x.size()), y.size());
    if (n > LIMIT) throw std::runtime_error("array too large");
    Array r(n);
    for (size_t i = 0; i < n; ++i) r[i] = mask[mask.size() == 1 ? 0 : i] != 0 ? x[x.size() == 1 ? 0 : i] : y[y.size() == 1 ? 0 : i];
    return {std::make_shared<const Array>(std::move(r))};
}
inline List diff(const List& a) { // r diff x; empty and singleton inputs give an empty array
    const Array& x = array(a[0]);
    Array r(x.size() ? x.size() - 1 : 0);
    for (size_t i = 0; i < r.size(); ++i) r[i] = x[i + 1] - x[i];
    return {std::make_shared<const Array>(std::move(r))};
}

// interface
inline void init(OpTable& ops) {
    ops["vec"] = Opcode{make, 0, -1, 1}; // allows empty vectors
    ops["add"] = Opcode{arith<std::plus<>, 0>, 1, -1, 1}; ops["sub"] = Opcode{arith<std::minus<>, 0>, 1, -1, 1};
    ops["mul"] = Opcode{arith<std::multiplies<>, 1>, 1, -1, 1}; ops["div"] = Opcode{arith<std::divides<>, 1>, 1, -1, 1};
    ops["abs"] = Opcode{unary<std::fabs>, 1, 1, 1}; ops["sqrt"] = Opcode{unary<std::sqrt>, 1, 1, 1};
    ops["exp"] = Opcode{unary<std::exp>, 1, 1, 1}; ops["log"] = Opcode{unary<std::log>, 1, 1, 1};
    ops["sin"] = Opcode{unary<std::sin>, 1, 1, 1}; ops["cos"] = Opcode{unary<std::cos>, 1, 1, 1};
    ops["tan"] = Opcode{unary<std::tan>, 1, 1, 1}; ops["atan"] = Opcode{unary<std::atan>, 1, 1, 1};
    ops["floor"] = Opcode{unary<std::floor>, 1, 1, 1};
    ops["range"] = Opcode{range, 1, 3, 1}; ops["cat"] = Opcode{cat, 2, -1, 1}; ops["slice"] = Opcode{aslice, 3, 4, 1};
    ops["len"] = Opcode{len, 1, 1, 1}; ops["fill"] = Opcode{fill, 2, 2, 1};
    ops["gather"] = Opcode{gather, 2, 2, 1}; ops["put"] = Opcode{put, 3, 3, 1};
    ops["sum"] = Opcode{sum, 1, 1, 1}; ops["min"] = Opcode{extremum<std::less<>>, 1, 1, 1}; ops["max"] = Opcode{extremum<std::greater<>>, 1, 1, 1};
    ops["eq"] = Opcode{compare<std::equal_to<>>, 2, 2, 1}; ops["ne"] = Opcode{compare<std::not_equal_to<>>, 2, 2, 1};
    ops["lt"] = Opcode{compare<std::less<>>, 2, 2, 1}; ops["le"] = Opcode{compare<std::less_equal<>>, 2, 2, 1};
    ops["gt"] = Opcode{compare<std::greater<>>, 2, 2, 1}; ops["ge"] = Opcode{compare<std::greater_equal<>>, 2, 2, 1};
    ops["where"] = Opcode{where, 3, 3, 1}; ops["diff"] = Opcode{diff, 1, 1, 1};
}
inline void repl(Env& env, OpTable& ops) {
    std::string line;
    size_t lineno = 0;
    while (std::cout << lineno +1 << " >> " << std::flush, std::getline(std::cin, line)) {
        ++lineno;
        try {
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            print(eval(read(line, ops), env), std::cout) << '\n';
        } catch (const std::exception& e) {
            std::cerr << "line " << lineno << ": " << e.what()
                      << "\n  " << line << '\n';
        }
    }
}
}
#endif // TEA_H
// eof
