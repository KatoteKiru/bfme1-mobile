"""Virtual file system: mount loose files + all *.big archives with SAGE priority rules.

Priority model (GeneralsGameCode, the engine BFME1 descends from):
  * FileSystem::openFile asks TheLocalFileSystem first -> loose files on disk beat every archive.
  * Win32BIGFileSystem::loadBigFilesFromDirectory("", "*.big") collects archives recursively into a
    FilenameList = std::set<AsciiString, less_than_nocase> -> archives are loaded in case-insensitive
    (lower-cased, _stricmp) path order.
  * ArchiveFileSystem::loadIntoDirectoryTree(overwrite=FALSE) appends each file at the END of a multimap
    key range, and lookups take the first element -> the FIRST loaded archive wins.
    That is why BFME patches are called "_patch*.big" ('_' 0x5F sorts before letters 0x61+) and mods use "!".
  * Within one archive, duplicate names: the first entry is kept here (inventory). The engine's
    ArchiveFile::addFile overwrites, so the later entry wins; terrainmap follows the engine.
  * Two archive paths that differ only by case mount once, as ZH FilenameList (a set). The
    byte-smaller path is kept (INI.BIG before ini.big).
NOTE: OpenSAGE's BigFileSystem uses "last archive wins" with alphabetical order, which would invert this for
BFME patches; we deliberately follow the EA engine.
"""
import os
from collections import OrderedDict

from .big import BigArchive, BigError


def sage_key(path):
    return path.replace('/', '\\').lower()


def norm(name):
    return name.replace('/', '\\').lower().lstrip('\\')


class VFile:
    __slots__ = ('path', 'source', 'entry', 'size', 'overridden', 'source_abs')

    def __init__(self, path, source, entry, size):
        self.path, self.source, self.entry, self.size = path, source, entry, size
        self.source_abs = None
        self.overridden = []   # list of (source label, entry/abs path, size) lower-priority versions

    def read(self, which=None):
        if which is None:
            if self.entry is None:
                with open(self.source_abs, 'rb') as f:
                    return f.read()
            return self.entry.read()
        return which.read()


class VFS:
    def __init__(self, root, include_loose=True, loose_exts=None, big_filter=None):
        self.root = os.path.abspath(root)
        self.archives = []
        self.archive_errors = []
        self.files = OrderedDict()      # norm path -> VFile (winning version)
        self.duplicates_in_archive = 0
        self.include_loose = include_loose
        self.loose_exts = loose_exts
        self._mount(big_filter)

    def _list_bigs(self):
        res = []
        for dp, dn, fn in os.walk(self.root):
            dn[:] = [d for d in dn if not d.startswith('.')]
            for f in fn:
                if f.lower().endswith('.big'):
                    rel = os.path.relpath(os.path.join(dp, f), self.root)
                    res.append(rel)

        def sort_key(rel):
            slash = rel.replace(os.sep, '/')
            return (sage_key(slash), slash)

        res.sort(key=sort_key)
        kept = []
        for rel in res:
            slash = rel.replace(os.sep, '/')
            if kept and sage_key(kept[-1].replace(os.sep, '/')) == sage_key(slash):
                continue
            kept.append(rel)
        return kept

    def _mount(self, big_filter):
        if self.include_loose:
            self._mount_loose()
        for rel in self._list_bigs():
            if big_filter and not big_filter(rel):
                continue
            try:
                arc = BigArchive(os.path.join(self.root, rel), label=rel.replace(os.sep, '/'))
            except (BigError, OSError) as e:
                self.archive_errors.append((rel, str(e)))
                continue
            self.archives.append(arc)
            for e in arc.entries:
                key = norm(e.name)
                cur = self.files.get(key)
                if cur is None:
                    self.files[key] = VFile(e.name.replace('/', '\\'), arc.label, e, e.size)
                else:
                    if cur.entry is not None and cur.entry.archive is arc:
                        self.duplicates_in_archive += 1
                    cur.overridden.append((arc.label, e, e.size))

    def _mount_loose(self):
        # Only data-like loose files count (the engine looks up relative paths such as Data\INI\...).
        for dp, dn, fn in os.walk(self.root):
            dn[:] = [d for d in dn if not d.startswith('.')]
            for f in fn:
                low = f.lower()
                if low.endswith(('.big', '.md', '.sha256', '.log', '.tmp', '.part', '.exe', '.dll')) or low.startswith('.'):
                    continue  # archives are mounted separately; copy-tool bookkeeping is not game data
                if self.loose_exts and os.path.splitext(low)[1] not in self.loose_exts:
                    continue
                ap = os.path.join(dp, f)
                rel = os.path.relpath(ap, self.root).replace(os.sep, '\\')
                vf = VFile(rel, '<loose>', None, os.path.getsize(ap))
                vf.source_abs = ap
                self.files[norm(rel)] = vf

    def get(self, path):
        return self.files.get(norm(path))

    def read(self, path):
        vf = self.get(path)
        if vf is None:
            return None
        if vf.entry is None:
            with open(vf.source_abs, 'rb') as f:
                return f.read()
        return vf.entry.read()

    def listdir_prefix(self, prefix):
        p = norm(prefix)
        return [k for k in self.files if k.startswith(p)]
