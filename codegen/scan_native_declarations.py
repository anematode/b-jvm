#!/usr/bin/env python
import re
import sys

def process_file(filename):
    export_regex = r'DECLARE(?:_ASYNC)?_NATIVE2?(?:_OVERLOADED)?\(.*?,\s*([\w$]+),\s*([\w$]+).*?(\d+)?\)\s*{'  # Define the actual regex pattern

    with open(filename, 'r', encoding='utf-8') as file:
        contents = file.read()

    matches = re.findall(export_regex, contents, flags=re.DOTALL)
    decls = ""
    calls = ""

    for [klass_name, method_name, overload_idx] in matches:
        overload_idx = 0 if overload_idx is '' else overload_idx
        struct_name = f"NATIVE_INFO_{klass_name}_{method_name}_{overload_idx}"
        push_call = f'''\t&{struct_name},'''


        calls += '\n' + push_call

        decls += f"extern native_t {struct_name};\n"

    return decls, calls

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(0)

    decls_ = ""
    function_body = ""
    for file in sys.argv[1].split(' '):
        decls, calls = process_file(file)

        if decls != "":
            decls_ += decls
            function_body += calls + "\n"

    print(f"""#include <bjvm.h>

{decls_}
const native_t *bjvm_natives[] = {{{function_body}}};
const size_t bjvm_natives_count = sizeof(bjvm_natives)/sizeof(*bjvm_natives);""")