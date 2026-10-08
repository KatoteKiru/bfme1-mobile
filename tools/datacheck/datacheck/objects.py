"""Resolve final module lists of Object/ChildObject/ObjectReskin after inheritance (OpenSAGE/Generals semantics).

* ChildObject X Parent / ObjectReskin X Parent: start from a clone of the parent's final modules, all marked
  "inherited"; a plain Object starts from DefaultThingTemplate (if defined), also inherited.
* Adding a module (ObjectDefinition.AddModuleData in OpenSAGE, ModuleInfo copiedFromDefault in Generals):
  inherited modules in the same slot are dropped when
    mode Default               -> their ModuleKinds overlap the new module's kinds (Body/Update/Die/Draw/...)
    mode OverrideableByLikeKind-> same module type
    mode InheritableModule     -> never (AlwaysInherit)
  then modules[tag] = new module.
* RemoveModule TAG / ReplaceModule TAG ... END remove the tag first.
"""
from collections import Counter, OrderedDict

SLOT = {'Behavior': 'B', 'Body': 'B', 'Draw': 'D', 'ClientUpdate': 'CU', 'ClientBehavior': 'CB'}
MODE = {'Default': 'Default', 'AddModule': 'Default', 'ReplaceModule': 'Default',
        'InheritableModule': 'AlwaysInherit', 'OverrideableByLikeKind': 'OverrideableByLikeKind'}


def _resolver(by_name, default):
    """final(o) -> OrderedDict tag -> module of definition o, parents looked up in by_name (lower name -> def)."""
    cache, missing = {}, Counter()

    def final(o, stack=()):
        key = id(o)
        if key in cache:
            return cache[key]
        base = None
        if o['parent']:
            p = by_name.get(o['parent'].lower())
            if p is None:
                missing[o['parent']] += 1
            elif p is not o and o['name'].lower() not in stack:
                base = final(p, stack + (o['name'].lower(),))
        elif default is not None and o is not default:
            base = final(default, stack + (o['name'].lower(),))
        mods = OrderedDict()
        if base:
            for tag, m in base.items():
                mods[tag] = dict(m, inherited=True)
        for op in o['ops']:
            if op[0] == 'remove':
                mods.pop(op[1].lower(), None)
                continue
            _, field, mtype, tag, kinds, wrapper, where = op
            slot = SLOT.get(field, field)
            for t, m in list(mods.items()):
                if not m.get('inherited') or m['slot'] != slot:
                    continue
                if m['mode'] == 'Default' and set(m['kinds']) & set(kinds):
                    del mods[t]
                elif m['mode'] == 'OverrideableByLikeKind' and m['type'] == mtype:
                    del mods[t]
            mods[tag.lower()] = {'slot': slot, 'field': field, 'type': mtype, 'kinds': kinds,
                                 'mode': MODE.get(wrapper, 'Default'), 'where': where, 'inherited': False}
        cache[key] = mods
        return mods
    return final, missing


def resolve(objects, only=None):
    by_name = {}
    for o in objects:
        if only and not only(o):
            continue
        by_name[o['name'].lower()] = o          # later definition wins (map.ini overrides etc.)
    final, missing = _resolver(by_name, by_name.get('defaultthingtemplate'))
    result = {}
    for name, o in by_name.items():
        result[o['name']] = final(o)
    return result, missing


def module_frequency(final_objs, names=None):
    """(instances Counter, objects Counter, example) of (field, module type) over final objects.
    names: optional set of lower-cased object names (e.g. units buildable in skirmish)."""
    c, objs, ex = Counter(), Counter(), {}
    for name, mods in final_objs.items():
        if names is not None and name.lower() not in names:
            continue
        seen = set()
        for m in mods.values():
            k = (m['field'], m['type'])
            c[k] += 1
            if k not in seen:
                objs[k] += 1
                seen.add(k)
            ex.setdefault(k, f"{name} ({m['where']})")
    return c, objs, ex


def _scopes(objects):
    """[(scope name, definitions)]: 'data\\ini' (global) and one scope per other file (map.ini), whose objects are
    resolved over the global ones (the map's definitions win, as the engine loads map.ini after data\\ini)."""
    glob = [o for o in objects if o['file'].lower().startswith('data\\ini')]
    per = OrderedDict()
    for o in objects:
        if not o['file'].lower().startswith('data\\ini'):
            per.setdefault(o['file'], []).append(o)
    return [('data\\ini', glob, glob)] + [(f, glob + objs, objs) for f, objs in per.items()]


def object_counts(objects):
    """Per definition: own and final (after inheritance) object-level field and module counts.

    Inheritance as in resolve(): ChildObject / ObjectReskin start from a copy of the parent's final state, a plain
    Object from DefaultThingTemplate (if defined). Fields: the final set of distinct object-level keys is the parent's
    set plus the definition's own keys (a key set again in the child replaces the parent's value, it is not added
    twice). Modules: resolve() semantics (AddModule / ReplaceModule / RemoveModule / InheritableModule /
    OverrideableByLikeKind, ModuleKinds slots). One row per definition, `effective` marks the definition that wins
    in its scope (later definition of the same name wins)."""
    rows = []
    for scope, pool, own in _scopes(objects):
        by_name = {}
        for o in pool:
            by_name[o['name'].lower()] = o
        default = by_name.get('defaultthingtemplate')
        mfinal, _missing = _resolver(by_name, default)
        fcache = {}

        def fields(o, stack=()):
            if id(o) in fcache:
                return fcache[id(o)]
            base = set()
            if o['parent']:
                p = by_name.get(o['parent'].lower())
                if p is not None and p is not o and o['name'].lower() not in stack:
                    base = fields(p, stack + (o['name'].lower(),))
            elif default is not None and o is not default:
                base = fields(default, stack + (o['name'].lower(),))
            res = set(base) | set(o.get('fields', ()))
            fcache[id(o)] = res
            return res

        for o in own:
            eff = by_name.get(o['name'].lower()) is o
            fm = mfinal(o)
            parent_missing = bool(o['parent']) and o['parent'].lower() not in by_name
            rows.append({
                'scope': scope, 'kind': o['kind'], 'name': o['name'], 'parent': o['parent'] or '',
                'at': f"{o.get('source') or ''}:{o['where']}", 'effective': int(eff),
                'parent_missing': int(parent_missing),
                'own_field_lines': sum(o.get('fields', {}).values()), 'own_fields': len(o.get('fields', ())),
                'final_fields': len(fields(o)),
                'own_module_adds': sum(1 for op in o['ops'] if op[0] == 'add'),
                'own_module_removes': sum(1 for op in o['ops'] if op[0] == 'remove'),
                'final_modules': len(fm), 'final_modules_inherited': sum(1 for m in fm.values() if m.get('inherited')),
            })
    return rows
