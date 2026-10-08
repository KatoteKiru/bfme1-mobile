"""Skirmish reachability: which objects a faction can actually get in a skirmish game.

Graph (all edges are taken from the FINAL objects after ChildObject/ObjectReskin inheritance):
  PlayerTemplate (PlayableSide = Yes): Side, BuildableHeroesMP, BuildableRingHeroesMP, StartingBuilding, StartingUnit*
    + every object named in some CastleToUnpackForFaction = <Side> <Object> ... (castles, camps, outposts, plots)
  Object -> CommandSet (top-level field, inherited)  + CommandSet/CommandSetAlt of CommandSetUpgrade-like modules
         -> HordeContain RankInfo UnitType:X, InitialPayload X, CastleToUnpackForFaction <Side> X
  CommandSet -> its CommandButtons
  CommandButton (Command in BUILD_COMMANDS) -> Object = ... (may list several)
Recursion until closure. Not followed (yet): ObjectCreationList spawns, SpecialPower summons, OCL upgrades,
map-placed objects (needs the 2.22 1v1 map list, see LIMITATIONS.md).
"""
import re
from collections import defaultdict, Counter, deque

BUILD_COMMANDS = {'unit_build', 'foundation_construct', 'dozer_construct', 'castle_unpack_explicit_object',
                  'castle_upgrade', 'revive'}
MODULE_REF_FIELDS = {'commandset', 'commandsetalt', 'initialpayload', 'castletounpackforfaction', 'rankinfo'}


def _toks(v):
    return [t for t in re.split(r'[\s,]+', v or '') if t]


def read_bases(vfs, object_names):
    """Castle/camp layouts: bases\\<name>\\<name>.bse (DataChunk/CkMp like .map, RefPack). Returns
    {layout name lower: sorted object template names placed in it}. Heuristic: every identifier in the decoded
    file that equals a known object name (template names are stored as plain ASCII in ObjectsList)."""
    import sys
    import os
    from . import chunks as probe
    names = {n.lower(): n for n in object_names}
    out = {}
    for k, vf in vfs.files.items():
        if not k.endswith('.bse'):
            continue
        try:
            d, _ = probe.map_bytes(vfs.read(k))
        except Exception:
            continue
        found = {names[t.decode('latin-1').lower()] for t in re.findall(rb'[A-Za-z0-9_]{3,}', d)
                 if t.decode('latin-1').lower() in names}
        out[os.path.splitext(k.rsplit('\\', 1)[-1])[0]] = sorted(found)
    return out


class Recruit:
    def __init__(self, records, final, objects, bases=None):
        self.bases = bases or {}
        self.final = {k.lower(): v for k, v in final.items()}   # name.lower() -> OrderedDict(tag -> module)
        self.top = defaultdict(dict)           # (kind, name) -> {field: (value, where)}
        self.blocks = defaultdict(lambda: defaultdict(list))   # kind -> name -> [(field, value, where)]
        self.module_refs = defaultdict(list)   # module 'where' -> [(field, value)]
        self.obj_by_name = {o['name'].lower(): o for o in objects if o['file'].lower().startswith('data\\ini')}
        obj_kinds = {'object', 'childobject', 'objectreskin'}
        mod_where = {}
        for key, field, raw, val, where in records:
            if not where.lower().startswith('data\\ini'):
                continue
            kind, name = key[0][0].lower(), key[0][1]
            if field == '<module>' and len(key) == 2:
                mod_where[key] = where
                continue
            if field.startswith('<'):
                continue
            if kind in obj_kinds:
                if len(key) == 2:
                    self.top[name].setdefault(field.lower(), (val, where))
                elif len(key) == 3 and field.lower() in MODULE_REF_FIELDS:
                    mw = mod_where.get(key[:2])
                    if mw:
                        self.module_refs[mw].append((field.lower(), val, where))
            else:
                self.blocks[kind][name].append((field, val, where))

    def obj_field(self, name, field, _depth=0):
        """Top-level field with ChildObject/ObjectReskin inheritance."""
        name = name.lower()
        if field in self.top.get(name, {}):
            return self.top[name][field]
        o = self.obj_by_name.get(name)
        if o and o['parent'] and _depth < 20:
            return self.obj_field(o['parent'], field, _depth + 1)
        return None

    def object_edges(self, name, side=None):
        out = []
        cs = self.obj_field(name, 'commandset')
        if cs:
            out.append(('commandset', cs[0], cs[1]))
        for tag, m in self.final.get(name.lower(), {}).items():
            for f, v, w in self.module_refs.get(m['where'], ()):
                t = _toks(v)
                if f in ('commandset', 'commandsetalt') and t:
                    out.append(('commandset', t[0], w))
                elif f == 'initialpayload' and t:
                    out.append(('object', t[0], w))
                elif f == 'rankinfo':
                    for x in t:
                        if x.lower().startswith('unittype:'):
                            out.append(('object', x.split(':', 1)[1], w))
                elif f == 'castletounpackforfaction' and len(t) > 1 and (side is None or t[0].lower() == side):
                    out.append(('object', t[1], w))
        return out

    def commandset_edges(self, cs):
        out = []
        for f, v, w in self.blocks['commandset'].get(cs.lower(), ()):
            if f.isdigit() and v:
                out.append(('button', _toks(v)[0], w))
        return out

    def button_edges(self, b):
        fields = self.blocks['commandbutton'].get(b.lower(), ())
        cmd = next((v.lower() for f, v, w in fields if f.lower() == 'command'), '')
        if cmd not in BUILD_COMMANDS:
            return []
        return [('object', x, w) for f, v, w in fields if f.lower() == 'object' for x in _toks(v)]

    def factions(self):
        res = {}
        castle = defaultdict(list)
        # CastleToUnpackForFaction lives in modules (CastleBehavior)
        for mw, refs in self.module_refs.items():
            for f, v, w in refs:
                t = _toks(v)
                if f == 'castletounpackforfaction' and len(t) > 1:
                    castle[t[0].lower()].append((t[1], w))
        for pt, fields in self.blocks['playertemplate'].items():
            d = {f.lower(): (v, w) for f, v, w in fields}
            if d.get('playableside', ('no',))[0].lower() not in ('yes', 'true', '1'):
                continue
            side = d.get('side', ('', ''))[0]
            roots = []
            for f in ('buildableheroesmp', 'buildableringheroesmp', 'startingbuilding'):
                if f in d:
                    roots += [(x, d[f][1]) for x in _toks(d[f][0])]
            roots += [(v[0], v[1]) for f, v in d.items() if f.startswith('startingunit')]
            roots += castle.get(side.lower(), [])
            res[pt] = {'side': side, 'roots': roots, 'castle_templates': castle.get(side.lower(), [])}
        return res

    def plot_roots(self):
        """Fallback roots: objects whose CommandSet offers FOUNDATION_CONSTRUCT / CASTLE_UNPACK* (building plots,
        castle/camp/outpost/economy flags). Castle/camp layouts themselves (Camp_*, Castle_*) are not INI objects."""
        cmds = {'foundation_construct', 'castle_unpack', 'castle_unpack_explicit_object'}
        btn = {b for b, f in self.blocks['commandbutton'].items()
               if any(x[0].lower() == 'command' and x[1].lower() in cmds for x in f)}
        css = {cs for cs, f in self.blocks['commandset'].items()
               if any(x[0].isdigit() and _toks(x[1]) and _toks(x[1])[0].lower() in btn for x in f)}
        out = []
        for n in self.top:
            cs = self.obj_field(n, 'commandset')
            if cs and cs[0].lower() in css:
                out.append((self.obj_by_name[n]['name'] if n in self.obj_by_name else n, cs[1]))
        return out

    def closure(self, roots, side=None):
        seen_obj, seen_cs, seen_b = {}, set(), set()
        q = deque(('object', n, w, None) for n, w in roots)
        missing = Counter()
        while q:
            kind, n, w, via = q.popleft()
            ln = n.lower()
            if kind == 'object':
                if ln in seen_obj:
                    continue
                if ln not in self.final:
                    if ln in self.bases:            # castle/camp layout (.bse): its placed objects
                        seen_obj.setdefault('<base>' + ln, None)
                        for x in self.bases[ln]:
                            q.append(('object', x, f'bases\\{ln}\\{ln}.bse', n))
                    else:
                        missing[n] += 1
                    continue
                seen_obj[ln] = (n, w, via)
                for e in self.object_edges(n, side):
                    q.append(e + (n,))
            elif kind == 'commandset':
                if ln in seen_cs:
                    continue
                seen_cs.add(ln)
                for e in self.commandset_edges(n):
                    q.append(e + (n,))
            elif kind == 'button':
                if ln in seen_b:
                    continue
                seen_b.add(ln)
                for e in self.button_edges(n):
                    q.append(e + (n,))
        return seen_obj, missing

    def classify(self, name):
        k = self.obj_field(name, 'kindof')
        ks = set(_toks(k[0].upper())) if k else set()
        if 'HERO' in ks:
            return 'hero'
        if 'STRUCTURE' in ks:
            return 'structure'
        if 'HORDE' in ks:
            return 'horde'
        if ks & {'INFANTRY', 'CAVALRY', 'MONSTER', 'MACHINE', 'SIEGE_WEAPON', 'SHIP', 'PORTER'}:
            return 'unit'
        return 'other'


def build(records, final, objects, bases=None):
    """Return per-faction reachable objects with class and module frequency (final objects)."""
    from .objects import module_frequency
    r = Recruit(records, final, objects, bases)
    out = {}
    plots = r.plot_roots()
    for pt, info in sorted(r.factions().items()):
        objs, missing = r.closure(info['roots'] + plots, info['side'].lower())
        layouts = sorted(k[6:] for k in objs if k.startswith('<base>'))
        objs = {k: v for k, v in objs.items() if not k.startswith('<base>')}
        side = info['side'].lower()
        hero_roots = {n.lower() for n, w in info['roots']}
        foreign = {}
        for ln in list(objs):
            sv = r.obj_field(ln, 'side')
            if ln not in hero_roots and sv and sv[0].lower() != side:
                foreign[ln] = objs.pop(ln)
        cls = {ln: r.classify(n) for ln, (n, w, via) in objs.items()}
        per = {}
        for group in ('hero', 'horde', 'unit', 'structure', 'other'):
            names = {ln for ln, c in cls.items() if c == group}
            c, o, ex = module_frequency(final, names=names)
            per[group] = {'objects': len(names), 'modules': o.most_common()}
        units = {ln for ln, c in cls.items() if c in ('hero', 'horde', 'unit')}
        c, o, ex = module_frequency(final, names=units)
        out[pt] = {'side': info['side'], 'roots': info['roots'], 'reachable': len(objs),
                   'other_side_dropped': len(foreign), 'plot_roots': len(plots), 'base_layouts_used': layouts,
                   'by_class': Counter(cls.values()), 'groups': per,
                   'units_and_heroes_modules': o.most_common(),
                   'heroes': sorted(objs[ln][0] for ln, c in cls.items() if c == 'hero'),
                   'missing_refs': dict(missing),
                   'objects': {objs[ln][0]: {'class': cls[ln], 'via': objs[ln][2], 'where': objs[ln][1]} for ln in objs}}
    return out
