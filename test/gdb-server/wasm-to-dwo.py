#!/usr/bin/env python

# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


"""Utility tools that extracts DWARF information encoded in a wasm module
produced by the LLVM toolchain, stores it in a separate ELF file with extension
.dwo
"""
import argparse
import logging
import sys
import os

def parse_args():
  parser = argparse.ArgumentParser(prog='extract.py', description=__doc__)
  parser.add_argument('input', help='wasm file')
  parser.add_argument('-dwo-', '--dwo', help='output dwo')
  parser.add_argument('-wasm-', '--wasm', help='output wasm')
  parser.add_argument('-module_url-', '--module_url', help='wasm module path')
  parser.add_argument('-symbols_url-', '--symbols_url', help='wasm symbols path')
  return parser.parse_args()

class DwarfSection:
  def __init__(self, name, buff):
    self.name = name
    self.bytes = buff
    self.length = len(buff)
    self.nameOffset = 0
    self.offset = 0

def read_var_uint(wasm, pos):
  n = 0
  shift = 0
  b = ord(wasm[pos:pos + 1])
  pos = pos + 1
  while b >= 128:
    n = n | ((b - 128) << shift)
    b = ord(wasm[pos:pos + 1])
    pos = pos + 1
    shift += 7
  return n + (b << shift), pos

def strip_debug_sections(wasm, module_url):
  logging.debug('Strip debug sections')

  result = []
  pos = 8
  stripped = wasm[:pos]
  while pos < len(wasm):
    section_start = pos
    section_id, pos_ = read_var_uint(wasm, pos)
    section_size, section_body = read_var_uint(wasm, pos_)
    section_end = section_start + section_size
    section_payload = wasm[section_body:section_body + section_size]
    pos = section_body + section_size
    if section_id == 0:
      name_len, name_pos = read_var_uint(wasm, section_body)
      name_end = name_pos + name_len
      name = wasm[name_pos:name_end]
      if name == "linking" or name == "sourceMappingURL" or name.startswith("reloc..debug_") or name.startswith(".debug_"):
        continue  # skip debug related sections
      if name == "name":
        payload_start = name_end;
        payload_size = section_end - payload_start
        subsection_pos = payload_start
        subsections = {}
        while (subsection_pos < payload_start + payload_size):
          subsection_type, subsection_pos = read_var_uint(wasm, subsection_pos)
          subsection_len, subsection_pos = read_var_uint(wasm, subsection_pos)
          subsections[subsection_type] = encode_uint_var(subsection_type) + encode_uint_var(subsection_len) + wasm[subsection_pos:subsection_pos + subsection_len]
          subsection_pos += subsection_len
        subsections[0] = create_name_subsection(0, module_url)

        name_section_payload = bytearray()
        for subsection_type in subsections:
          name_section_payload += subsections[subsection_type]

        section = create_custom_section("name", name_section_payload)
        stripped = stripped + section
        continue
    stripped = stripped + wasm[section_start:pos]
  return stripped

def strip_wasm_sections(wasm):
  logging.debug('Strip wasm sections')
  result = []
  pos = 8
  #stripped = wasm[:pos]
  while pos < len(wasm):
    section_start = pos
    section_id, pos_ = read_var_uint(wasm, pos)
    section_size, section_body = read_var_uint(wasm, pos_)
    pos = section_body + section_size
    if section_id == 0:
      name_len, name_pos = read_var_uint(wasm, section_body)
      name_end = name_pos + name_len
      name = wasm[name_pos:name_end]
      if name.startswith(".debug_"):
        result.append(DwarfSection(name, wasm[name_end:pos]))
  return result

def encode_uint_var(n):
  result = bytearray()
  while n > 127:
    result.append(128 | (n & 127))
    n = n >> 7
  result.append(n)
  return bytes(result)

def encode_uint16(n):
  result = bytearray()
  result.append(n & 0xff)
  result.append((n >> 8) & 0xff)
  return bytes(result)

def encode_uint32(n):
  result = bytearray()
  result.append(n & 0xff)
  result.append((n >> 8) & 0xff)
  result.append((n >> 16) & 0xff)
  result.append((n >> 24) & 0xff)
  return bytes(result)

def encode_uint64(n):
  result = bytearray()
  result.append(n & 0xff)
  result.append((n >> 8) & 0xff)
  result.append((n >> 16) & 0xff)
  result.append((n >> 24) & 0xff)
  result.append((n >> 32) & 0xff)
  result.append((n >> 40) & 0xff)
  result.append((n >> 48) & 0xff)
  result.append((n >> 56) & 0xff)
  return bytes(result)

def encode_string(s):
  result = encode_uint_var(len(s)) + s
  return bytes(result)

def create_custom_section(section_name, payload):
  section_content = encode_uint_var(len(section_name)) + section_name + payload
  return encode_uint_var(0) + encode_uint_var(len(section_content)) + section_content

def create_name_subsection(subsection_type, payload):
  subsection_content = encode_uint_var(len(payload)) + payload
  return encode_uint_var(subsection_type) + encode_uint_var(len(subsection_content)) + subsection_content

def get_elf_header_ident(is64bit):
  logging.debug('Append elf header ident')
  result = bytearray()
  result.append(0x7f)
  result.append('E')
  result.append('L')
  result.append('F')
  result.append(0x02 if is64bit else 0x01) # EI_CLASS
  result.append(0x01) # EI_DATA
  result.append(0x01) # EI_VERSION
  result.append(0x00) # EI_OSABI
  result.append(0x00) # ELFOSABI_SYSV // todo: wasm
  result += bytearray(b'\x00\x00\x00\x00\x00\x00\x00')
  return result

def get_elf_header(is64bit, sectionHeaderOffset, numberOfSections):
  logging.debug('Append elf header')
  result = get_elf_header_ident(is64bit)
  result += encode_uint16(3) # e_type: ET_DYN
  result += encode_uint16(0xf8) # e_machine: EM_WASM32
  result += encode_uint32(1) # e_version
  result += encode_uint32(0) # e_entry
  result += encode_uint32(0) # e_phoff
  result += encode_uint32(sectionHeaderOffset) # e_shoff
  result += encode_uint32(0) # e_flags
  result += encode_uint16(0x34) # e_ehsize
  result += encode_uint16(0) # e_phentsize
  result += encode_uint16(0) # e_phnum
  result += encode_uint16(0x28) # e_shentsize
  result += encode_uint16(numberOfSections + 2) # e_shnum
  result += encode_uint16(1) # e_shstrndx
  return result

def get_section_header(sectionName, sectionNameOffset, sectionOffset, sectionSize):
  SHF_MASKPROC = 0xF00000000
  SHF_MERGE = 1 << 4
  SHF_STRINGS = 1 << 5
  SHT_PROGBITS = 1

  mask = SHF_MASKPROC
  entsize = 0
  if sectionName == ".debug_str":
    mask |= (SHF_MERGE | SHF_STRINGS)
    entsize = 1
  result = bytearray()
  result += encode_uint32(sectionNameOffset) # sh_name
  result += encode_uint32(SHT_PROGBITS) # sh_type
  result += encode_uint32(mask) # sh_flags
  result += encode_uint32(0) # sh_addr
  result += encode_uint32(sectionOffset) # sh_offset
  result += encode_uint32(sectionSize) # sh_size
  result += encode_uint32(0) # sh_link
  result += encode_uint32(0) # sh_info
  result += encode_uint32(1) # sh_addralign
  result += encode_uint32(entsize) # sh_entsize
  return result

def get_strtab_section_header(sectionNameOffset, sectionOffset, sectionSize):
  SHT_STRTAB = 3

  result = bytearray()
  result += encode_uint32(sectionNameOffset) # sh_name
  result += encode_uint32(SHT_STRTAB) # sh_type
  result += encode_uint32(0) # sh_flags
  result += encode_uint32(0) # sh_addr
  result += encode_uint32(sectionOffset) # sh_offset
  result += encode_uint32(sectionSize) # sh_size
  result += encode_uint32(0) # sh_link
  result += encode_uint32(0) # sh_info
  result += encode_uint32(1) # sh_addralign
  result += encode_uint32(0) # sh_entsize
  return result

def main():
  # example: python wasm-to-dwo.py --dwo foo.dwo --wasm foo.wasm --module_url g:\test\foo.wasm --symbols_url g:\test\foo.dwo foo.wasm

  options = parse_args()
  with open(options.input, 'rb') as infile:
    wasm_input = infile.read()

  with open(options.wasm, 'wb') as outfile_wasm:
    wasm = strip_debug_sections(wasm_input, options.module_url)
    wasm += create_custom_section("sourceMappingURL", encode_string(options.symbols_url))
    outfile_wasm.write(wasm)

  with open(options.dwo, 'wb') as outfile_dwo:
    sections = strip_wasm_sections(wasm_input)
    sectionHeaderOffset = 0x34
    strtabBytes = bytearray()
    strtabBytes.append('\0')
    for section in sections:
      section.nameOffset = len(strtabBytes)
      section.offset = sectionHeaderOffset
      sectionHeaderOffset += section.length
      strtabBytes.extend(section.name.encode('latin-1'))
      strtabBytes.extend("\0")
    strtabNameOffset = len(strtabBytes)
    strtabBytes.extend(".strtab\0".encode('latin-1'))
    sectionHeaderOffset += len(strtabBytes)
    padding = (4 - (sectionHeaderOffset & 0x03)) & 0x03
    sectionHeaderOffset += padding
    hdr = get_elf_header(False, sectionHeaderOffset, len(sections))
    outfile_dwo.write(hdr)
    for section in sections:
      outfile_dwo.write(bytes(section.bytes))
    strtabOffset = outfile_dwo.tell()
    outfile_dwo.write(strtabBytes)
    while padding > 0:
      outfile_dwo.write('\0')
      padding = padding - 1
    padding = 0x28
    while padding > 0:
      outfile_dwo.write('\0')
      padding = padding - 1
    outfile_dwo.write(get_strtab_section_header(strtabNameOffset, strtabOffset, len(strtabBytes)))
    for section in sections:
      outfile_dwo.write(get_section_header(section.name, section.nameOffset, section.offset, section.length))

  logging.debug('Done')
  return 0
if __name__ == '__main__':
  logging.basicConfig(level=logging.DEBUG if os.environ.get('EMCC_DEBUG') else logging.INFO)
  sys.exit(main())
