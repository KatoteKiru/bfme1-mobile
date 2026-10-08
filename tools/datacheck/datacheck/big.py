"""BIG archive reader (BIGF / BIG4), as used by SAGE games (Generals, ZH, BFME1).

Layout (checked against GeneralsGameCode Win32BIGFileSystem::openArchiveFile and OpenSAGE BigArchive):
  0  char[4]  magic "BIGF" (BFME/Generals) or "BIG4"
  4  uint32   archive size   -- LITTLE-endian in the EA writer (Generals reads it without betoh); informational only
  8  uint32   number of entries   (big-endian)
  12 uint32   offset of first file / header size (big-endian)
  16 entries: uint32 offset (BE), uint32 size (BE), NUL-terminated path (ASCII, '\\' separators)
Placeholder archives of < 4 bytes ("??") are treated as empty (OpenSAGE behaviour).
"""
import struct


class BigError(Exception):
    pass


class BigEntry:
    __slots__ = ('archive', 'name', 'offset', 'size', 'index')

    def __init__(self, archive, name, offset, size, index):
        self.archive, self.name, self.offset, self.size, self.index = archive, name, offset, size, index

    def read(self):
        return self.archive.read_entry(self)


class BigArchive:
    def __init__(self, path, label=None):
        self.path = path
        self.label = label or path
        self.magic = None
        self.declared_size = None
        self.first_offset = None
        self.entries = []
        self.warnings = []
        self._read_header()

    def _read_header(self):
        with open(self.path, 'rb') as f:
            data = f.read(16)
            if len(data) < 4:
                if data[:2] == b'??' or len(data) == 0:
                    self.magic = 'EMPTY'
                    return
                raise BigError(f'{self.path}: too small')
            self.magic = data[:4].decode('latin-1')
            if self.magic not in ('BIGF', 'BIG4'):
                raise BigError(f'{self.path}: bad magic {self.magic!r}')
            if len(data) < 16:
                raise BigError(f'{self.path}: truncated header')
            self.declared_size = struct.unpack('<I', data[4:8])[0]
            count, self.first_offset = struct.unpack('>II', data[8:16])
            f.seek(0, 2)
            fsize = f.tell()
            hdr_len = max(self.first_offset, 16)
            if hdr_len > fsize:
                raise BigError(f'{self.path}: first offset {hdr_len} beyond file size {fsize}')
            f.seek(0)
            hdr = f.read(hdr_len)
        pos = 16
        for i in range(count):
            if pos + 8 > len(hdr):
                raise BigError(f'{self.path}: entry table truncated at entry {i}/{count}')
            off, size = struct.unpack('>II', hdr[pos:pos + 8])
            pos += 8
            end = hdr.find(b'\0', pos)
            if end < 0:
                raise BigError(f'{self.path}: unterminated name at entry {i}')
            name = hdr[pos:end].decode('latin-1')
            pos = end + 1
            if off + size > fsize:
                self.warnings.append(f'entry {name} out of bounds ({off}+{size} > {fsize})')
            self.entries.append(BigEntry(self, name, off, size, i))
        if self.declared_size not in (fsize,):
            be = struct.unpack('>I', struct.pack('<I', self.declared_size))[0]
            if be != fsize:
                self.warnings.append(f'header size {self.declared_size} != file size {fsize}')

    def read_entry(self, e):
        with open(self.path, 'rb') as f:
            f.seek(e.offset)
            return f.read(e.size)


def write_big(path, files, magic=b'BIGF'):
    """Write a BIG archive (for tests / synthetic data). files: list of (name, bytes)."""
    names = [n.encode('latin-1') for n, _ in files]
    hdr_len = 16 + sum(8 + len(n) + 1 for n in names)
    off = hdr_len
    table = b''
    for (n, data), nb in zip(files, names):
        table += struct.pack('>II', off, len(data)) + nb + b'\0'
        off += len(data)
    total = off
    with open(path, 'wb') as f:
        f.write(magic + struct.pack('<I', total) + struct.pack('>II', len(files), hdr_len))
        f.write(table)
        for _, data in files:
            f.write(data)
