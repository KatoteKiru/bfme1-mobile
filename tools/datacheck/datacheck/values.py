"""Locale-independent value normalisation for INI diffs (never uses locale or float() formatting).

SAGE INI numbers are always '.'-decimal (the engine uses atof/strtod in the "C" locale). We parse with
decimal.Decimal on the literal text, so LC_ALL=ru_RU.UTF-8 (comma decimal separator) cannot change results.
Normalisations (OpenSAGE semantics where relevant):
  12 / 12.0 / 12.000      -> 12            (Decimal, trailing zeros stripped)
  50%                     -> 0.5           (IniParser.ParsePercentage divides by 100)
  X:1.50 / R:255 / Min:1  -> X:1.5         (prefixed numbers as in ParseVector3/ParseColorRgb)
  Yes/YES/yes             -> yes           (ParseBoolean is case-insensitive)
  1500ms / 1.5s / 1500    -> durations stay as written unit-less ms; '1.5s'/'1500ms' suffixes -> ms number
Everything else: whitespace collapsed, case kept (asset names are case-insensitive in SAGE, compared lower-cased).
"""
import re
from decimal import Context, Decimal, InvalidOperation

CTX = Context(prec=200)   # exact for any INI literal; independent of the thread's default context

NUM = re.compile(r'^[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?$')
PREFIXED = re.compile(r'^([A-Za-z_]\w*):(.*)$')
DUR = re.compile(r'^([+-]?(?:\d+\.?\d*|\.\d+))(ms|s|sec|msec)$', re.I)


def parse_number(text):
    """Decimal from an INI numeric literal, or None. Locale-independent."""
    t = text.strip()
    if t.endswith('f') and NUM.match(t[:-1]):   # some files carry C-style 1.0f
        t = t[:-1]
    if not NUM.match(t):
        return None
    try:
        return Decimal(t)
    except InvalidOperation:
        return None


def fmt(d):
    d = d.normalize(CTX)
    if d == 0:
        return "0"
    # format(..., 'f') never rounds and works for any exponent (quantize fails beyond the context precision,
    # e.g. 1e40 or a 30-digit literal in patch data)
    return format(d, 'f')


def norm_token(tok):
    d = parse_number(tok)
    if d is not None:
        return fmt(d)
    if tok.endswith('%'):
        d = parse_number(tok[:-1])
        if d is not None:
            return fmt(CTX.divide(d, 100))
    m = DUR.match(tok)
    if m:
        d = Decimal(m.group(1))
        unit = m.group(2).lower()
        return fmt(CTX.multiply(d, 1000) if unit in ('s', 'sec') else d)
    m = PREFIXED.match(tok)
    if m and m.group(2):
        return m.group(1).lower() + ':' + norm_token(m.group(2))
    low = tok.lower()
    if low in ('yes', 'no', 'true', 'false'):
        return {'true': 'yes', 'false': 'no'}.get(low, low)
    return low


def normalize(value):
    return ' '.join(norm_token(t) for t in re.split(r'[\s=]+', value.strip()) if t)
