#!/usr/bin/env python3

# This script opens a BCDB file directly and prints out all the cached results
# for a given function.

import cbor2
import pprint
import sqlite3
import sys
import argparse

def str2bool(v):
    return v.lower() in ['true', '1', 't']

parser = argparse.ArgumentParser()
parser.add_argument('database_file', help='Path to the database file')
parser.add_argument('fn_name', help='Name of the function')
parser.add_argument('--exit_code', help='If given, only calls with these exit codes will be printed ', nargs='*', type=int)
parser.add_argument('--forward_valid', type=str2bool)
parser.add_argument('--forward_timeout', type=str2bool)
parser.add_argument('--backward_valid', type=str2bool)
parser.add_argument('--backward_timeout', type=str2bool)
parser.add_argument('--translated_first', type=str2bool)
parser.add_argument('--translated_second', type=str2bool)
parser.add_argument('--identical_alive_ir', type=str2bool)

cl_args = parser.parse_args()

function_name = cl_args.fn_name
conn = sqlite3.connect(cl_args.database_file)
cursor = conn.cursor()

# A reference to one of the values in the BCDB.
class Ref:
    def __init__(self, ref):
        self.ref = ref
    def __str__(self):
        return f'Ref({self.ref})'
    def __repr__(self):
        return f'Ref({self.ref})'

# Load a value. All values are stored in the "blob" table.
def load_value(ref):
    cursor.execute('SELECT type, content FROM blob WHERE vid=?', (int(ref.ref),))
    typ, content = cursor.fetchone()
    if typ == 0:
        # Binary data stored directly.
        return content
    elif typ == 2:
        # Value stored as CBOR.
        # References to other values are stored as strings with CBOR tag 39.
        def tag_hook(decoder, tag, shareable_index=None):
            if tag.tag == 39:
                return Ref(tag.value)
            return tag
        return cbor2.loads(content, tag_hook=tag_hook)
    else:
        assert False, f'Unsupported value type {typ}'

# returns True if input result object matches the filter
def filter_result(result):
    if cl_args.exit_code and not result['exit_code'] in cl_args.exit_code:
        return False

    if cl_args.forward_valid is not None and result.get('forward_valid') != cl_args.forward_valid:
        return False
    if cl_args.forward_timeout is not None and result.get('forward_timeout') != cl_args.forward_timeout:
        return False
    if cl_args.backward_valid is not None and result.get('backward_valid') != cl_args.backward_valid:
        return False
    if cl_args.backward_timeout is not None and result.get('backward_timeout') != cl_args.backward_timeout:
        return False
    if cl_args.translated_first is not None and result.get('translated_first') != cl_args.translated_first:
        return False
    if cl_args.translated_second is not None and result.get('translated_second') != cl_args.translated_second:
        return False
    if cl_args.identical_alive_ir is not None and result.get('identical_alive_ir') != cl_args.identical_alive_ir:
        return False

    return True

# Look up the function ID based on its name.
cursor.execute('SELECT fid FROM func WHERE name = ?', (function_name,))
fid, = cursor.fetchone()

# Function call results are stored in the "call" table. There is a separate row
# for each argument, so if we stored the call f(101,102,103)=200, the table
# rows might look like this:
#
# - cid=0 fid=50 parent=NULL arg=101 result=NULL
# - cid=1 fid=50 parent=0    arg=102 result=NULL
# - cid=2 fid=50 parent=1    arg=103 result=200

parent_queue = [(None, [])] # queue of (parent's cid, parent's args up to this point)
while parent_queue:
    parent, parent_args = parent_queue.pop()
    cursor.execute('SELECT cid, arg, result FROM call WHERE fid=? AND parent IS ?', (fid, parent))
    for cid, arg, result in cursor.fetchall():
        args = parent_args + [Ref(str(arg))]
        parent_queue.append((cid, args))
        if result is not None:
            result = load_value(Ref(str(result)))
            if filter_result(result):
                print(f'{function_name}({", ".join(str(arg) for arg in args)}) = {pprint.pformat(result)}')
