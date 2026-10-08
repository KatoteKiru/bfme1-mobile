"""Tiny C# lexing helpers for static extraction (comments, braces, top-level split)."""
import re

STR_OR_COMMENT = re.compile(
    r'(@"(?:""|[^"])*"|\$?"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')|(//[^\n]*|/\*.*?\*/)', re.S)


def strip_comments(src):
    def repl(m):
        if m.group(1) is not None:
            return m.group(1)
        return re.sub(r'[^\n]', ' ', m.group(2))
    return STR_OR_COMMENT.sub(repl, src)


def _skip_string(text, j):
    n = len(text)
    if j > 0 and text[j - 1] == '@':
        j += 1
        while j < n:
            if text[j] == '"':
                if j + 1 < n and text[j + 1] == '"':
                    j += 2
                    continue
                return j
            j += 1
        return j
    j += 1
    while j < n and text[j] != '"' and text[j] != '\n':
        j += 2 if text[j] == '\\' else 1
    return j


def match_brace(text, i, open_ch='{', close_ch='}'):
    depth, n, j = 0, len(text), i
    while j < n:
        c = text[j]
        if c == '"':
            j = _skip_string(text, j)
        elif c == "'":
            j += 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return -1


def split_top(text, sep=','):
    parts, depth, start, j, n = [], 0, 0, 0, len(text)
    while j < n:
        c = text[j]
        if c == '"':
            j = _skip_string(text, j)
        elif c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(text[start:j])
            start = j + 1
        j += 1
    if text[start:].strip():
        parts.append(text[start:])
    return parts


def find_statement_end(text, i):
    depth, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i = _skip_string(text, i)
        elif c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        elif c == ';' and depth <= 0:
            return i
        i += 1
    return n


def parse_entries(init_text):
    """Content between { } of a dictionary initializer -> [(key, expr)]."""
    res = []
    for part in split_top(init_text):
        p = part.strip()
        if not p.startswith('{'):
            continue
        end = match_brace(p, 0)
        kv = split_top(p[1:end])
        if len(kv) < 2:
            continue
        km = re.match(r'\s*"([^"]*)"\s*$', kv[0])
        if km:
            res.append((km.group(1), ','.join(kv[1:]).strip()))
    return res
