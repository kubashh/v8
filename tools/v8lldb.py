#!/usr/bin/env python3
#
# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
This script uses Torque object layout information, as provided by debug_helper,
to improve the printing of V8 object types when using basic commands such as
'frame variable'. For
local debugging in builds that include debug printers, you can just use
lldb_commands.py, but this script works in cases when you can't or don't want to
disrupt the debuggee's state, such as core dumps, and in cases where V8 was
built without debug printers, such as production builds of Chromium and Node.

IMPORTANT WARNING:

If you just use `frame variable` with no further arguments, this program will
cause the debugger to become unresponsive! You must either limit the printing
depth using the -D / --depth option on every `frame variable` command, or use
lldb version >=15, in which case this program can automatically set the global
depth limit.

To use:

1. Build v8_debug_helper_shared with exactly the same code and gn arguments as
   the program being debugged.
2. Add your build output directory to LD_LIBRARY_PATH, or modify this script's
   LoadLibrary call to use an absolute path.
3. In an lldb session, run `command script import /path/to/v8lldb.py`

Tested on Ubuntu using the lldb-12 package.
"""

import ctypes
from enum import Enum
from enum import auto
import lldb
import lldb.formatters.Logger
import os
import platform

debug_helper = ctypes.cdll.LoadLibrary('libv8_debug_helper_shared.so')

# ==============================================================================
# C++ library interop start.
#
# This section duplicates the contents of debug_helper.h in a way that Python
# can use. Comments are not duplicated; see debug_helper.h for details.
# ==============================================================================


class MemoryAccessResult(Enum):
  kOk = 0
  kAddressNotValid = auto()
  kAddressValidButInaccessible = auto()


class TypeCheckResult(Enum):
  kSmi = 0
  kWeakRef = auto()
  kUsedMap = auto()
  kKnownMapPointer = auto()
  kUsedTypeHint = auto()
  kUnableToDecompress = auto()
  kObjectPointerInvalid = auto()
  kObjectPointerValidButInaccessible = auto()
  kMapPointerInvalid = auto()
  kMapPointerValidButInaccessible = auto()
  kUnknownInstanceType = auto()
  kUnknownTypeHint = auto()


class PropertyKind(Enum):
  kSingle = 0
  kArrayOfKnownSize = auto()
  kArrayOfUnknownSizeDueToInvalidMemory = auto()
  kArrayOfUnknownSizeDueToValidButInaccessibleMemory = auto()


class PropertyBase(ctypes.Structure):
  _fields_ = [("name", ctypes.c_char_p), ("type", ctypes.c_char_p),
              ("decompressed_type", ctypes.c_char_p)]


class StructProperty(PropertyBase):
  _fields_ = [("offset", ctypes.c_size_t), ("num_bits", ctypes.c_uint8),
              ("shift_bits", ctypes.c_uint8)]


class ObjectProperty(PropertyBase):
  _fields_ = [("address", ctypes.c_void_p), ("num_values", ctypes.c_size_t),
              ("size", ctypes.c_size_t), ("num_struct_fields", ctypes.c_size_t),
              ("struct_fields", ctypes.POINTER(ctypes.POINTER(StructProperty))),
              ("kind", ctypes.c_int)]


class ObjectPropertiesResult(ctypes.Structure):
  _fields_ = [("type_check_result", ctypes.c_int), ("brief", ctypes.c_char_p),
              ("type", ctypes.c_char_p), ("num_properties", ctypes.c_size_t),
              ("properties", ctypes.POINTER(ctypes.POINTER(ObjectProperty))),
              ("num_guessed_types", ctypes.c_size_t),
              ("guessed_types", ctypes.POINTER(ctypes.c_char_p))]


class StackFrameResult(ctypes.Structure):
  _fields_ = [("num_properties", ctypes.c_size_t),
              ("properties", ctypes.POINTER(ctypes.POINTER(ObjectProperty)))]


MemoryAccessor = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p,
                                  ctypes.c_void_p, ctypes.c_size_t)


class HeapAddresses(ctypes.Structure):
  _fields_ = [("map_space_first_page", ctypes.c_void_p),
              ("old_space_first_page", ctypes.c_void_p),
              ("read_only_space_first_page", ctypes.c_void_p),
              ("any_heap_pointer", ctypes.c_void_p)]


class ClassList(ctypes.Structure):
  _fields_ = [("num_class_names", ctypes.c_size_t),
              ("class_names", ctypes.POINTER(ctypes.c_char_p))]


debug_helper._v8_debug_helper_GetObjectProperties.restype = ctypes.POINTER(
    ObjectPropertiesResult)
debug_helper._v8_debug_helper_GetObjectProperties.argtypes = [
    ctypes.c_void_p, MemoryAccessor,
    ctypes.POINTER(HeapAddresses), ctypes.c_char_p
]

debug_helper._v8_debug_helper_Free_ObjectPropertiesResult.argtypes = [
    ctypes.POINTER(ObjectPropertiesResult)
]

debug_helper._v8_debug_helper_GetStackFrame.restype = ctypes.POINTER(
    StackFrameResult)
debug_helper._v8_debug_helper_GetStackFrame.argtypes = [
    ctypes.c_void_p, MemoryAccessor
]

debug_helper._v8_debug_helper_Free_StackFrameResult.argtypes = [
    ctypes.POINTER(StackFrameResult)
]

debug_helper._v8_debug_helper_ListObjectClasses.restype = ctypes.POINTER(
    ClassList)
debug_helper._v8_debug_helper_ListObjectClasses.argtypes = []

debug_helper._v8_debug_helper_BitsetName.restype = ctypes.c_char_p
debug_helper._v8_debug_helper_BitsetName.argtypes = [ctypes.c_uint64]

# debug_helper.h defines a couple of smart pointer types which automatically
# call the Free functions as necessary to clean up memory allocated by
# debug_helper. A Python equivalent is types usable with `with`.


class GetObjectProperties:

  def __init__(self, *args):
    self.args = args
    self.result = None

  def __enter__(self):
    self.result = debug_helper._v8_debug_helper_GetObjectProperties(*self.args)
    return self.result

  def __exit__(self, exc_type, exc_value, traceback):
    debug_helper._v8_debug_helper_Free_ObjectPropertiesResult(self.result)
    self.result = None


class GetStackFrame:

  def __init__(self, *args):
    self.args = args
    self.result = None

  def __enter__(self):
    self.result = debug_helper._v8_debug_helper_GetStackFrame(*self.args)
    return self.result

  def __exit__(self, exc_type, exc_value, traceback):
    debug_helper._v8_debug_helper_Free_StackFrameResult(self.result)
    self.result = None


# ==============================================================================
# C++ library interop end.
# ==============================================================================


# Synthetic children provider for v8::internal::Handle. It will create a single
# field named 'Value', which is the appropriately typed subclass of
# v8::internal::Object.
class HandleProvider:

  def __init__(self, valobj, internal_dict):
    self.valobj = valobj

    # Determine the data type of the value stored in this Handle. For the public
    # API (v8::Local), we'll just use v8::internal::Object.
    self.data_type = valobj.GetTarget().FindFirstType('v8::internal::Object')

    # It seems like valobj.GetType().GetTemplateArgumentType(0) would return the
    # correct type for v8::internal::Handle and v8::internal::MaybeHandle, but
    # it returns None for unknown reasons. Instead, we'll find the member
    # Handle::PatchValue and get its argument type. For MaybeHandle, there's an
    # extra step to first find the corresponding Handle type, using the return
    # type of MaybeHandle::ToHandleChecked.
    handle_type = valobj.GetType()
    if handle_type.GetName() == "v8::internal::MaybeHandle":
      for i in range(0, handle_type.GetNumberOfMemberFunctions()):
        fn = handle_type.GetMemberFunctionAtIndex(i)
        if fn.GetName() == 'ToHandleChecked':
          handle_type = fn.GetReturnType()

    if handle_type.GetName() == "v8::internal::Handle":
      for i in range(0, handle_type.GetNumberOfMemberFunctions()):
        fn = handle_type.GetMemberFunctionAtIndex(i)
        if fn.GetName() == 'PatchValue':
          self.data_type = fn.GetArgumentTypeAtIndex(0)

  def num_children(self):
    return 1

  def get_child_index(self, name):
    if name == 'Value':
      return 0
    return -1

  def get_child_at_index(self, index):
    if index != 0:
      return None
    location = self.valobj.GetChildMemberWithName('location_').Dereference()
    value = self.valobj.CreateValueFromData('Value', location.GetData(),
                                            self.data_type)
    return value

  def update(self):
    # True means that nothing will ever change; False means that things could
    # change. Since the get_child_at_index implementation includes reading
    # memory from the debuggee, False is the safer choice.
    return False


# The target to read memory from. Must be set before using g_memory_accessor.
g_target = None


def ReadMemory(address, destination, byte_count):
  logger = lldb.formatters.Logger.Logger()
  error = lldb.SBError()
  bytes_read = g_target.ReadMemory(
      lldb.SBAddress(address, g_target), byte_count, error)
  if len(bytes_read) != byte_count:
    logger >> error
    return MemoryAccessResult.kAddressNotValid.value
  ctypes.memmove(destination, bytes_read, byte_count)
  return MemoryAccessResult.kOk.value


g_memory_accessor = MemoryAccessor(ReadMemory)


# Simulate looking up a type by name within the v8::internal namespace.
def LookupPossiblyQualifiedType(target, type):
  result = target.FindFirstType('v8::internal::' + type)
  if result:
    return result
  result = target.FindFirstType('v8::' + type)
  if result:
    return result
  result = target.FindFirstType(type)
  if result:
    return result
  return None


# Wrapper function for GetObjectProperties which was provided by debug_helper.
def GetObjectPropertiesWrapper(valobj):
  # Set the global target variable which will be used by the memory accessor.
  global g_target
  g_target = valobj.GetTarget()

  obj = valobj.GetChildMemberWithName('ptr_').GetValueAsUnsigned()
  type_hint = valobj.GetType().GetName().encode('utf-8')
  heap_addresses = HeapAddresses(0, 0, 0, 0)

  # For instances of TaggedValue, debug_helper also needs to know where in
  # memory the value was stored so that it can decompress the TaggedValue.
  # This does not work for tagged values found in InstructionStream objects
  # since they're stored in a separate pointer cage.
  if valobj.GetType().GetName() == "v8::internal::TaggedValue":
    heap_addresses.any_heap_pointer = valobj.GetLoadAddress()

  return GetObjectProperties(obj, g_memory_accessor,
                             ctypes.pointer(heap_addresses), type_hint)


# Synthetic children provider for v8::internal::Object and its subclasses. It
# will create children as specified by the debug_helper library.
class ObjectProvider:

  def __init__(self, valobj, internal_dict):
    self.valobj = valobj

    # The following will be set during update():
    self.children = []
    self.child_indexes = {}

  def num_children(self):
    return len(self.children)

  def get_child_index(self, name):
    return self.child_indexes.get(name, -1)

  def get_child_at_index(self, index):
    return self.children[index]

  def update(self):
    logger = lldb.formatters.Logger.Logger()

    # Start the list of children with the ptr_ field from TaggedImpl, because
    # otherwise GetObjectSummary can't find it.
    self.children = [self.valobj.GetChildMemberWithName('ptr_')]
    self.child_indexes = {'ptr_': 0}

    with GetObjectPropertiesWrapper(self.valobj) as props:
      props = props[0]
      for i in range(0, props.num_properties):
        prop = props.properties[i][0]
        prop_name = prop.name.decode('utf-8')
        prop_type_name = prop.type.decode('utf-8')
        is_struct = len(prop_type_name) == 0

        if not is_struct and prop.num_struct_fields > 0:
          logger >> (
              'not implemented: bitfield structs. treating as basic type ' +
              prop_type_name)

        if prop.kind == PropertyKind.kSingle.value:
          if is_struct:
            self.add_struct(prop_name, prop.address, prop.num_struct_fields,
                            prop.struct_fields)
          else:
            self.add_child(prop_name, prop.address, prop_type_name)
        elif prop.kind == PropertyKind.kArrayOfKnownSize.value:
          for j in range(0, prop.num_values):
            item_name = prop_name + '[' + str(j) + ']'
            item_address = prop.address + j * prop.size
            if is_struct:
              self.add_struct(item_name, item_address, prop.num_struct_fields,
                              prop.struct_fields)
            else:
              self.add_child(item_name, item_address, prop_type_name)
        else:
          logger >> 'not implemented: arrays of unknown length'

    # True means that nothing will ever change; False means that things could
    # change. The result from debug_helper didn't indicate whether the result
    # can change in the future, so it's safest to assume that it can.
    return False

  # The following functions are helpers not required by the interface for
  # specifying synthetic children.

  def add_child(self, name, address, type_name, num_bits=0, shift_bits=0):
    logger = lldb.formatters.Logger.Logger()
    if num_bits != 0 or shift_bits != 0:
      logger >> 'not implemented: bitfields'
      return
    prop_type = LookupPossiblyQualifiedType(g_target, type_name)
    if prop_type == None:
      logger >> ('unable to find type ' + type_name)
      return
    child = self.valobj.CreateValueFromAddress(name, address, prop_type)
    self.children.append(child)
    self.child_indexes[name] = len(self.children) - 1

  def add_struct(self, name, address, num_fields, fields):
    for i in range(0, num_fields):
      field = fields[i][0]
      type_name = field.type.decode('utf-8')
      field_name = name + '.' + field.name.decode('utf-8')
      self.add_child(field_name, address + field.offset, type_name,
                     field.num_bits, field.shift_bits)


def GetObjectSummary(valobj, internal_dict):
  with GetObjectPropertiesWrapper(valobj) as props:
    return props[0].brief.decode('utf-8')


def RegisterObjectType(debugger, type_name):
  # Register the synthetic children provider which represents the fields of the
  # object type.
  debugger.HandleCommand("type synthetic add " + type_name +
                         " --skip-pointers --skip-references --python-class " +
                         __name__ + ".ObjectProvider")
  # The summary function provides a short string representation of the value.
  # The "expand" options means that lldb should show both the summary string and
  # the fields; otherwise the synthetic children would be ignored.
  debugger.HandleCommand(
      'type summary add --skip-pointers --skip-references --expand ' +
      '--python-function ' + __name__ + '.GetObjectSummary ' + type_name)


# This function is called by lldb during "command script import".
def __lldb_init_module(debugger, internal_dict):
  # To enable logging, uncomment the following line.
  # lldb.formatters.Logger._lldb_formatters_debug_level = 2
  logger = lldb.formatters.Logger.Logger()
  logger >> ('Loading ' + __name__)

  # Register the synthetic children provider for v8::internal::Object and all of
  # its subclasses.
  object_classes = debug_helper._v8_debug_helper_ListObjectClasses()[0]
  for i in range(0, object_classes.num_class_names):
    RegisterObjectType(debugger, object_classes.class_names[i].decode("utf-8"))
  # Object isn't included in the list from ListObjectClasses, which only
  # includes subclasses of HeapObject.
  RegisterObjectType(debugger, 'v8::internal::Object')
  # TaggedValue isn't technically a type of Object, but is necessary for
  # interpreting compressed pointers.
  RegisterObjectType(debugger, 'v8::internal::TaggedValue')

  # Register the synthetic children provider for Handles and similar classes.
  for class_name in [
      "v8::Local", "v8::MaybeLocal", "v8::internal::Handle",
      "v8::internal::MaybeHandle"
  ]:
    debugger.HandleCommand("type synthetic add " + class_name +
                           " --python-class " + __name__ + ".HandleProvider")

  # Check whether the debugger supports setting the maximum recursion depth,
  # as added in
  # https://github.com/llvm/llvm-project/commit/2f9fc576be206bd5c4fddfec5f89fceb3554a8d6
  if hasattr(lldb.SBTarget(), 'GetMaximumDepthOfChildrenToDisplay'):
    debugger.HandleCommand("set set target.max-children-depth 2")
