#!/usr/bin/env python3
"""Reject ELF DSOs that old QNX dlsym cannot search.

Old QNX dlsym resolves a caller LR against the loaded object's text_addr /
text_size, and the loader takes that range from the FIRST PT_LOAD only. A modern
LLD rosegment layout (R metadata / RX text / RW data) therefore puts the proxy
body outside the searched range, so dlsym returns NULL even though the export
exists. Link such DSOs with --no-rosegment and require:

  * ELF32, little-endian, EM_ARM
  * exactly two PT_LOAD: first R+X and never W at p_vaddr 0, second R+W and not X
  * p_filesz <= p_memsz, page-congruent p_offset/p_vaddr, no shared mapped page
  * every SHF_ALLOC+SHF_EXECINSTR section, and every defined STT_FUNC symbol with
    nonzero size, inside the first load's file range
  * every SHF_ALLOC section inside some load (SHT_NOBITS against p_memsz only)

validate(path) returns a JSON-compatible dict (ok/reasons plus sha256, loads and
executable counts) and raises ValueError only when the file cannot be read or is
not an ELF at all. The CLI takes one or more paths, prints the dicts as JSON and
exits 1 when any file is incompatible.
"""
import argparse
import hashlib
import io
import json
import sys
from pathlib import Path

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile

PAGE = 0x1000
PF_X, PF_W, PF_R = 0x1, 0x2, 0x4
SHF_ALLOC, SHF_EXECINSTR = 0x2, 0x4
FIRST_LOAD_RULE = 'the loader takes text_addr/text_size from the first PT_LOAD only'


def flags(value):
    return ''.join(letter if value & bit else '-' for bit, letter in ((PF_R, 'R'), (PF_W, 'W'), (PF_X, 'X')))


def page(load):
    align = load['p_align']
    return align if align >= PAGE and align & (align - 1) == 0 else PAGE


def fits(load, address, size, mapped=False):
    end = load['p_vaddr'] + (load['p_memsz'] if mapped else load['p_filesz'])
    return load['p_vaddr'] <= address and address + size <= end


def mapped_pages(load):
    end = load['p_vaddr'] + load['p_memsz']
    return load['p_vaddr'] - load['p_vaddr'] % PAGE, end + -end % PAGE


def sample(items, limit=4):
    items = list(items)
    return ', '.join(items[:limit]) + (' (+%d more)' % (len(items) - limit) if len(items) > limit else '')


def validate(path):
    path = Path(path)
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ValueError('%s: cannot read file (%s)' % (path, exc)) from exc
    try:
        elf = ELFFile(io.BytesIO(data))
        segments = list(elf.iter_segments())
    except ELFError as exc:
        raise ValueError('%s: not a parseable ELF file (%s)' % (path, exc)) from exc

    reasons, checks = [], {}

    def check(name, ok, reason=None):
        checks[name] = bool(ok)
        if not ok and reason:
            reasons.append(reason)
        return ok

    check('elf32_little_arm', elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_ARM',
          'not ELF32 little-endian ARM (class %d, %s, %s)' % (
              elf.elfclass, 'little-endian' if elf.little_endian else 'big-endian', elf['e_machine']))

    loads = []
    for index, seg in enumerate([s for s in segments if s['p_type'] == 'PT_LOAD']):
        loads.append({'index': index, 'p_offset': int(seg['p_offset']), 'p_vaddr': int(seg['p_vaddr']),
                      'p_filesz': int(seg['p_filesz']), 'p_memsz': int(seg['p_memsz']),
                      'p_flags': int(seg['p_flags']), 'flags': flags(int(seg['p_flags'])),
                      'p_align': int(seg['p_align'])})
        load = loads[-1]
        load['page_congruent'] = load['p_offset'] % page(load) == load['p_vaddr'] % page(load)
        load['vaddr_end'] = load['p_vaddr'] + load['p_memsz']
        check('load%d_filesz_within_memsz' % index, load['p_filesz'] <= load['p_memsz'],
              'PT_LOAD[%d] p_filesz %#x exceeds p_memsz %#x' % (index, load['p_filesz'], load['p_memsz']))
        check('load%d_page_congruent' % index, load['page_congruent'],
              'PT_LOAD[%d] p_offset %#x and p_vaddr %#x are not congruent modulo %#x'
              % (index, load['p_offset'], load['p_vaddr'], page(load)))

    check('exactly_two_loads', len(loads) == 2,
          'expected exactly two PT_LOAD, found %d (%s)' % (len(loads), ', '.join(load['flags'] for load in loads)))

    first = loads[0] if loads else None
    if len(loads) == 2:
        second = loads[1]
        check('first_load_executable', first['p_flags'] & (PF_R | PF_X) == (PF_R | PF_X),
              'first PT_LOAD is not executable (flags %s, vaddr %#x..%#x): %s, so no caller LR can be resolved'
              % (first['flags'], first['p_vaddr'], first['p_vaddr'] + first['p_filesz'], FIRST_LOAD_RULE))
        check('first_load_not_writable', not first['p_flags'] & PF_W,
              'first PT_LOAD %s is writable; the searched text range must be read+execute' % first['flags'])
        check('first_load_vaddr_zero', first['p_vaddr'] == 0,
              'first PT_LOAD p_vaddr is %#x, not 0' % first['p_vaddr'])
        check('second_load_writable_not_executable', second['p_flags'] == (PF_R | PF_W),
              'second PT_LOAD %s is not writable without execute (expected RW only)' % second['flags'])
        first_pages, second_pages = mapped_pages(first), mapped_pages(second)
        check('loads_do_not_share_pages', first_pages[1] <= second_pages[0] or second_pages[1] <= first_pages[0],
              'the two PT_LOAD share mapped pages: [%#x,%#x) and [%#x,%#x)'
              % (first_pages[0], first_pages[1], second_pages[0], second_pages[1]))

    sections = list(elf.iter_sections())
    exec_sections = [s for s in sections if s['sh_flags'] & SHF_ALLOC and s['sh_flags'] & SHF_EXECINSTR]
    exec_rows = [{'name': s.name, 'address': int(s['sh_addr']), 'size': int(s['sh_size']),
                  'outside_first_load': first is None or not fits(first, int(s['sh_addr']), int(s['sh_size']))}
                 for s in exec_sections]
    outside_sections = [row for row in exec_rows if row['outside_first_load']]
    check('exec_sections_inside_first_load', not outside_sections,
          '%d executable section(s) outside first PT_LOAD %s: %s' % (
              len(outside_sections),
              '[%#x,%#x)' % (first['p_vaddr'], first['p_vaddr'] + first['p_filesz']) if first else '(no PT_LOAD)',
              sample('.%s [%#x,%#x)' % (row['name'].lstrip('.') or '?', row['address'], row['address'] + row['size'])
                     for row in outside_sections)))

    orphans = [s.name for s in sections if s['sh_flags'] & SHF_ALLOC and int(s['sh_size']) and not any(
        fits(load, int(s['sh_addr']), int(s['sh_size']), mapped=s['sh_type'] == 'SHT_NOBITS') for load in loads)]
    check('alloc_sections_inside_a_load', not orphans,
          '%d allocated section(s) outside every PT_LOAD: %s' % (len(orphans), sample(orphans)))

    function_counts, outside_functions = {}, []
    for table_name in ('.dynsym', '.symtab'):
        table = elf.get_section_by_name(table_name)
        if table is None:
            continue
        count = 0
        for sym in table.iter_symbols():
            index = sym['st_shndx']
            if sym['st_info']['type'] != 'STT_FUNC' or index == 'SHN_UNDEF' or isinstance(index, str):
                continue  # undefined, SHN_ABS and SHN_COMMON symbols have no loaded address
            section = elf.get_section(index) if index < elf.num_sections() else None
            if section is None or not section['sh_flags'] & SHF_ALLOC or not int(sym['st_size']):
                continue  # only allocated functions with a known size are addressable exports
            count += 1
            address = int(sym['st_value']) & ~1  # the ARM Thumb bit is not part of the address
            if first is None or not fits(first, address, int(sym['st_size'])):
                outside_functions.append('%s:%s@%#x' % (table_name, sym.name or '?', address))
        function_counts[table_name] = count
    check('functions_inside_first_load', not outside_functions,
          '%d sized defined function symbol(s) outside first PT_LOAD: %s'
          % (len(outside_functions), sample(outside_functions)))

    return {'ok': not reasons, 'path': str(path), 'reasons': reasons, 'checks': checks,
            'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data),
            'elf_class': elf.elfclass, 'little_endian': bool(elf.little_endian),
            'machine': elf['e_machine'], 'type': elf['e_type'],
            'load_count': len(loads), 'loads': loads,
            'first_load_executable': bool(first['p_flags'] & PF_X) if first else False,
            'executable_section_count': len(exec_rows), 'executable_sections': exec_rows,
            'function_symbol_counts': function_counts,
            'function_symbol_count': sum(function_counts.values()),
            'functions_outside_first_load': len(outside_functions),
            'functions_outside_sample': outside_functions[:8]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('elf', nargs='+', type=Path, help='QNX shared object to validate')
    args = parser.parse_args(argv)
    status = 0
    for path in args.elf:
        try:
            result = validate(path)
        except ValueError as exc:
            print(json.dumps({'ok': False, 'path': str(path), 'reasons': [str(exc)]}, indent=2))
            print('FAIL %s' % exc, file=sys.stderr)
            status = 1
            continue
        print(json.dumps(result, indent=2))
        if result['ok']:
            print('PASS %s: %d PT_LOAD, first %s at %#x with %d executable section(s) and %d function symbol(s)'
                  % (path, result['load_count'], result['loads'][0]['flags'], result['loads'][0]['p_vaddr'],
                     result['executable_section_count'], result['function_symbol_count']), file=sys.stderr)
        else:
            print('FAIL %s: %s' % (path, '; '.join(result['reasons'])), file=sys.stderr)
            status = 1
    return status


if __name__ == '__main__':
    raise SystemExit(main())
