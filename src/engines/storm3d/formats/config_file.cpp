// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/config_file.h"

#include "common/text.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace reluva::storm3d {

namespace {

constexpr int kMaxIncludeDepth = 16;

const std::set<std::string> kBuiltinTypes = {"int", "float", "double", "char", "bool", "long", "short"};
const std::set<std::string> kQualifiers = {"const", "unsigned", "signed", "static"};

bool isIdentStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

} // namespace

struct ConfigFile::Token {
    enum class Type { kIdent, kNumber, kString, kPunct, kEnd };
    Type type = Type::kEnd;
    std::string text; // an identifier, a string without quotes, or a punctuation mark
    double number = 0;
    std::string where; // file:line
};

// Reads declarations from tokens into a file, or one value from a define's;
// names in expressions are looked up in `lookup`.
class ConfigFile::Parser {
public:
    Parser(const ConfigFile &lookup, ConfigFile *out, std::vector<Token> tokens)
        : _lookup(lookup), _out(out), _tokens(std::move(tokens)) {
        _tokens.push_back({Token::Type::kEnd, "", 0, _tokens.empty() ? "" : _tokens.back().where});
    }

    Error declarations() {
        if (!_out)
            return Error::fail("nowhere to put declarations");
        while (!at(Token::Type::kEnd))
            if (Error err = declaration())
                return err;
        return Error::success();
    }

    // A define's text as a value: an expression or strings, and nothing after.
    bool value(Value &out) {
        if (!initializer(out) || !_error.empty() || !at(Token::Type::kEnd))
            return false;
        return true;
    }

private:
    const Token &peek(size_t ahead = 0) const { return _tokens[std::min(_pos + ahead, _tokens.size() - 1)]; }
    bool at(Token::Type type) const { return peek().type == type; }
    bool atPunct(const char *p) const { return at(Token::Type::kPunct) && peek().text == p; }
    bool atIdent(const char *word) const { return at(Token::Type::kIdent) && peek().text == word; }
    const Token &next() { return _tokens[std::min(_pos++, _tokens.size() - 1)]; }

    Error fail(const std::string &what) const {
        const Token &t = peek();
        return Error::fail(t.where + ": " + what +
                           (t.type == Token::Type::kEnd ? " at the end" : ", at '" + t.text + "'"));
    }

    bool expect(const char *p) {
        if (!atPunct(p))
            return false;
        ++_pos;
        return true;
    }

    Error declaration() {
        if (atIdent("struct") && peek(1).type == Token::Type::kIdent && peek(2).text == "{")
            return structDefinition();
        while (at(Token::Type::kIdent) && kQualifiers.count(peek().text))
            ++_pos;
        if (!at(Token::Type::kIdent))
            return fail("a type was expected");
        // ART_CFG.h sets two variables with no type
        // (DETAIL_1_cinematic_show_grid = 0;), names the game looks up.
        const bool untyped = peek(1).text == "=";
        const std::string type = untyped ? std::string("float") : next().text;
        const auto structIt = _out->_structs.find(type);
        if (!kBuiltinTypes.count(type) && structIt == _out->_structs.end())
            return fail("'" + type + "' isn't a type");
        if (!at(Token::Type::kIdent))
            return fail("a name was expected");
        const std::string name = next().text;
        int dimensions = 0;
        while (expect("[")) {
            Value size;
            if (!atPunct("]") && !expression(size))
                return fail(_error.empty() ? "an array size was expected" : _error);
            if (!expect("]"))
                return fail("']' was expected");
            ++dimensions;
        }
        Value value;
        if (expect("=")) {
            if (!initializer(value)) {
                if (_unknownName.empty())
                    return fail(_error.empty() ? "a value was expected" : _error);
                // ART_CFG.h uses STAR_FAR_CLIP_PLANE before setting it: the
                // game's value for it then isn't known, so this one is left
                // unset rather than guessed.
                _out->_problems.push_back(peek().where + ": " + name + " is left unset: " + _error);
                _unknownName.clear();
                _error.clear();
                while (!atPunct(";") && !at(Token::Type::kEnd))
                    ++_pos;
                expect(";");
                return Error::success();
            }
        } else if (dimensions > 0 || structIt != _out->_structs.end()) {
            value.kind = Value::Kind::kList;
        }
        if (!expect(";"))
            return fail("';' was expected");
        // A char array holds a string, not a list; an array of structures
        // may be written as one flat list, which is grouped field by field.
        const bool charArray = type == "char" && dimensions == 1;
        if (structIt != _out->_structs.end() && value.kind == Value::Kind::kList)
            group(value, structIt->second.fields.size(), dimensions > 0);
        else if (charArray && value.kind == Value::Kind::kNumber)
            value = Value{Value::Kind::kString, 0, "", {}};
        _out->_variables[name] = std::move(value);
        return Error::success();
    }

    Error structDefinition() {
        _pos += 1;
        const std::string name = next().text;
        ++_pos; // {
        Struct s;
        while (!atPunct("}")) {
            while (at(Token::Type::kIdent) && kQualifiers.count(peek().text))
                ++_pos;
            if (!at(Token::Type::kIdent) || peek(1).type != Token::Type::kIdent)
                return fail("a field was expected in struct " + name);
            _pos += 1;
            s.fields.push_back(next().text);
            while (expect("[")) {
                Value size;
                if (!expression(size) || !expect("]"))
                    return fail("a field's array size was expected");
            }
            if (!expect(";"))
                return fail("';' was expected after a field");
        }
        ++_pos; // }
        expect(";");
        _out->_structs[name] = std::move(s);
        return Error::success();
    }

    // A structure's fields, or an array's records, from a flat list.
    static void group(Value &list, size_t fields, bool array) {
        if (fields == 0 || !array)
            return;
        bool flat = true;
        for (const Value &item : list.items)
            flat = flat && item.kind != Value::Kind::kList;
        if (!flat)
            return;
        std::vector<Value> records;
        for (size_t i = 0; i < list.items.size(); i += fields) {
            Value record{Value::Kind::kList, 0, "", {}};
            for (size_t j = i; j < i + fields && j < list.items.size(); ++j)
                record.items.push_back(std::move(list.items[j]));
            records.push_back(std::move(record));
        }
        list.items = std::move(records);
    }

    bool initializer(Value &out) {
        if (expect("{")) {
            out = Value{Value::Kind::kList, 0, "", {}};
            while (!atPunct("}")) {
                Value item;
                if (!initializer(item))
                    return false;
                out.items.push_back(std::move(item));
                if (!expect(","))
                    break;
            }
            return expect("}");
        }
        if (at(Token::Type::kString)) {
            out = Value{Value::Kind::kString, 0, "", {}};
            while (at(Token::Type::kString))
                out.text += next().text; // adjacent strings join, as in C
            return true;
        }
        return expression(out);
    }

    bool expression(Value &out) {
        if (!term(out))
            return false;
        while (atPunct("+") || atPunct("-")) {
            const bool add = next().text == "+";
            Value rhs;
            if (!term(rhs))
                return false;
            out.number = add ? out.number + rhs.number : out.number - rhs.number;
        }
        return true;
    }

    bool term(Value &out) {
        if (!factor(out))
            return false;
        while (atPunct("*") || atPunct("/")) {
            const bool multiply = next().text == "*";
            Value rhs;
            if (!factor(rhs))
                return false;
            out.number = multiply ? out.number * rhs.number : out.number / rhs.number;
        }
        return true;
    }

    bool factor(Value &out) {
        out = Value{};
        if (expect("-")) {
            if (!factor(out))
                return false;
            out.number = -out.number;
            return true;
        }
        if (expect("+"))
            return factor(out);
        if (expect("(")) {
            if (!expression(out))
                return false;
            return expect(")");
        }
        if (at(Token::Type::kNumber)) {
            out.number = next().number;
            return true;
        }
        if (at(Token::Type::kIdent)) {
            const std::string name = next().text;
            const Value *v = _lookup.find(name);
            if (!v || v->kind != Value::Kind::kNumber) {
                _error = "'" + name + "' isn't a number set before";
                _unknownName = name;
                return false;
            }
            out.number = v->number;
            return true;
        }
        return false;
    }

    const ConfigFile &_lookup;
    ConfigFile *_out;
    std::vector<Token> _tokens;
    size_t _pos = 0;
    std::string _error;
    std::string _unknownName; // a name an expression used that has no value yet
};

Error ConfigFile::load(std::string_view name, const Loader &loader) {
    _variables.clear();
    _defines.clear();
    _structs.clear();
    _problems.clear();
    _defineValues.clear();
    std::vector<Token> tokens;
    if (Error err = preprocess(name, loader, 0, tokens))
        return err;
    return Parser(*this, this, std::move(tokens)).declarations();
}

Error ConfigFile::preprocess(std::string_view name, const Loader &loader, int depth, std::vector<Token> &tokens) {
    if (depth > kMaxIncludeDepth)
        return Error::fail(std::string(name) + ": includes nest more than " + std::to_string(kMaxIncludeDepth) +
                           " deep");
    std::string text;
    if (Error err = loader(name, text))
        return err;

    size_t line = 1;
    bool lineStart = true;
    bool inData = false; // inside #DATA ... #END_DATA
    const auto where = [&] { return std::string(name) + ":" + std::to_string(line); };
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\n') {
            ++line;
            lineStart = true;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            i = text.find('\n', i);
            if (i == std::string::npos)
                break;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const size_t end = text.find("*/", i + 2);
            for (size_t j = i; j < (end == std::string::npos ? text.size() : end); ++j)
                line += text[j] == '\n';
            i = end == std::string::npos ? text.size() : end + 2;
            continue;
        }
        if (c == '#' && lineStart) {
            size_t end = text.find('\n', i);
            if (end == std::string::npos)
                end = text.size();
            std::string directive = text::trim(std::string_view(text).substr(i, end - i));
            if (const size_t comment = directive.find("//"); comment != std::string::npos)
                directive = text::trim(std::string_view(directive).substr(0, comment));
            const size_t space = directive.find_first_of(" \t");
            const std::string word = directive.substr(0, space);
            const std::string rest = space == std::string::npos ? "" : text::trim(directive.substr(space));
            i = end;
            if (word == "#END_DATA") {
                inData = false;
            } else if (inData) {
                continue;
            } else if (word == "#include") {
                const size_t open = rest.find('"');
                const size_t close = rest.find('"', open + 1);
                if (open == std::string::npos || close == std::string::npos) {
                    _problems.push_back(where() + ": " + directive);
                    continue;
                }
                if (Error err = preprocess(rest.substr(open + 1, close - open - 1), loader, depth + 1, tokens))
                    return Error::fail(where() + ": " + err.message);
            } else if (word == "#define") {
                size_t nameEnd = 0;
                while (nameEnd < rest.size() && (isIdentStart(rest[nameEnd]) || isDigit(rest[nameEnd])))
                    ++nameEnd;
                if (nameEnd == 0 || (nameEnd < rest.size() && rest[nameEnd] == '('))
                    _problems.push_back(where() + ": " + directive); // a macro with arguments: none seen
                else
                    _defines[rest.substr(0, nameEnd)] = text::trim(rest.substr(nameEnd));
            } else if (word == "#DATA") {
                inData = true;
                _problems.push_back(where() + ": a #DATA block, skipped");
            } else {
                _problems.push_back(where() + ": " + directive);
            }
            continue;
        }
        lineStart = false;
        if (inData) {
            i = text.find('\n', i);
            if (i == std::string::npos)
                break;
            continue;
        }

        Token t;
        t.where = where();
        if (c == '"') {
            // No escapes: the files write paths as ".\missions".
            const size_t end = text.find('"', i + 1);
            if (end == std::string::npos)
                return Error::fail(t.where + ": a string isn't closed");
            t.type = Token::Type::kString;
            for (size_t j = i + 1; j < end; ++j) {
                if (text[j] == '\n')
                    ++line;
                if (text[j] != '\r')
                    t.text += text[j];
            }
            i = end + 1;
        } else if (isIdentStart(c)) {
            const size_t start = i;
            while (i < text.size() && (isIdentStart(text[i]) || isDigit(text[i])))
                ++i;
            t.type = Token::Type::kIdent;
            t.text = text.substr(start, i - start);
        } else if (isDigit(c) || (c == '.' && i + 1 < text.size() && isDigit(text[i + 1]))) {
            const char *start = text.c_str() + i;
            char *end = nullptr;
            t.number = std::strtod(start, &end);
            i += size_t(end - start);
            if (i < text.size() && (text[i] == 'f' || text[i] == 'F'))
                ++i;
            t.type = Token::Type::kNumber;
            t.text = text.substr(size_t(start - text.c_str()), size_t(end - start));
        } else {
            t.type = Token::Type::kPunct;
            t.text = std::string(1, c);
            ++i;
        }
        tokens.push_back(std::move(t));
    }
    return Error::success();
}

const ConfigFile::Value *ConfigFile::find(std::string_view name) const {
    const std::string key(name);
    if (const auto it = _variables.find(key); it != _variables.end())
        return &it->second;
    if (const auto it = _defineValues.find(key); it != _defineValues.end())
        return &it->second;
    const auto define = _defines.find(key);
    // A define that names itself, directly or through others, has no value.
    if (define == _defines.end() || !_evaluating.insert(key).second)
        return nullptr;
    struct Done {
        std::set<std::string> &names;
        std::string key;
        ~Done() { names.erase(key); }
    } done{_evaluating, key};
    // A define's text, tokenized as a file of its own and read as a value.
    ConfigFile scratch;
    std::vector<Token> tokens;
    const Loader one = [&](std::string_view, std::string &text) {
        text = define->second;
        return Error::success();
    };
    if (scratch.preprocess(key, one, 0, tokens))
        return nullptr;
    Value value;
    // Its names are this file's: another define, or a variable.
    if (!Parser(*this, nullptr, std::move(tokens)).value(value))
        return nullptr;
    return &(_defineValues[key] = std::move(value));
}

double ConfigFile::number(std::string_view name, double fallback) const {
    const Value *v = find(name);
    return v && v->kind == Value::Kind::kNumber ? v->number : fallback;
}

std::string ConfigFile::string(std::string_view name, std::string_view fallback) const {
    const Value *v = find(name);
    return v && v->kind == Value::Kind::kString ? v->text : std::string(fallback);
}

} // namespace reluva::storm3d
