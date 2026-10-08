// BFME1 Mobile — tests
// SPDX-License-Identifier: GPL-3.0-or-later
// Just enough JSON to read the OpenSAGE oracle output (tools/oracle/out/*.json).
#pragma once

#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bfmetest {

struct Json {
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;
	bool b = false;
	double num = 0;
	std::string str;
	std::vector<Json> arr;
	std::map<std::string, Json> obj;

	const Json& operator[](const std::string& key) const
	{
		static const Json null;
		auto it = obj.find(key);
		return it == obj.end() ? null : it->second;
	}
	bool has(const std::string& key) const { return obj.count(key) != 0; }
	long long asInt() const { return (long long)num; }

	static Json parse(const std::string& text)
	{
		size_t pos = 0;
		Json v = parseValue(text, pos);
		return v;
	}
	static Json parseFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) throw std::runtime_error("cannot open " + path);
		std::stringstream ss;
		ss << f.rdbuf();
		return parse(ss.str());
	}

private:
	static void ws(const std::string& t, size_t& p)
	{
		while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t')) ++p;
	}
	static void expect(const std::string& t, size_t& p, char c)
	{
		ws(t, p);
		if (p >= t.size() || t[p] != c) throw std::runtime_error(std::string("JSON: expected '") + c + "' at " + std::to_string(p));
		++p;
	}
	static void putUtf8(std::string& out, unsigned cp)
	{
		if (cp < 0x80) out += char(cp);
		else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
		else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
	}
	static std::string parseString(const std::string& t, size_t& p)
	{
		expect(t, p, '"');
		std::string out;
		while (p < t.size() && t[p] != '"') {
			char c = t[p++];
			if (c != '\\') { out += c; continue; }
			char e = t[p++];
			switch (e) {
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			case 'r': out += '\r'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'u': putUtf8(out, unsigned(std::strtoul(t.substr(p, 4).c_str(), nullptr, 16))); p += 4; break;
			default: out += e; break;
			}
		}
		++p;
		return out;
	}
	static Json parseValue(const std::string& t, size_t& p)
	{
		ws(t, p);
		Json v;
		if (p >= t.size()) throw std::runtime_error("JSON: unexpected end");
		const char c = t[p];
		if (c == '{') {
			v.type = Object;
			++p;
			ws(t, p);
			if (t[p] == '}') { ++p; return v; }
			for (;;) {
				std::string key = parseString(t, p);
				expect(t, p, ':');
				v.obj[key] = parseValue(t, p);
				ws(t, p);
				if (t[p] == ',') { ++p; continue; }
				expect(t, p, '}');
				return v;
			}
		}
		if (c == '[') {
			v.type = Array;
			++p;
			ws(t, p);
			if (t[p] == ']') { ++p; return v; }
			for (;;) {
				v.arr.push_back(parseValue(t, p));
				ws(t, p);
				if (t[p] == ',') { ++p; continue; }
				expect(t, p, ']');
				return v;
			}
		}
		if (c == '"') { v.type = String; v.str = parseString(t, p); return v; }
		if (t.compare(p, 4, "true") == 0) { v.type = Bool; v.b = true; p += 4; return v; }
		if (t.compare(p, 5, "false") == 0) { v.type = Bool; p += 5; return v; }
		if (t.compare(p, 4, "null") == 0) { p += 4; return v; }
		char* end = nullptr;
		v.type = Number;
		v.num = std::strtod(t.c_str() + p, &end);
		if (end == t.c_str() + p) throw std::runtime_error("JSON: bad value at " + std::to_string(p));
		p = size_t(end - t.c_str());
		return v;
	}
};

} // namespace bfmetest
