# Generate trampolines for interpreter -> JIT and JIT -> interpreter. These trampolines are also generated at runtime
# for combinations not covered here.

# Read desired_trampolines.txt. Each row is <return type><args>    then <frequency>. Generate trampolines for the most
# common combinations.

reads = open("desired_trampolines.txt").read().split("\n")
desired = []

for read in reads:
    if len(read) == 0:
        continue
    return_type = read[0]
    args = read[1:].split("\t")[0]
    frequency = int(read.split("\t")[1])

    desired.append((return_type, args, frequency))

wasm_types = {
    "V": "\\x00",
    "D": "\\x7c",
    "F": "\\x7d",
    "J": "\\x7e",
    "I": "\\x7f"
}

c_types = {
    "V": "void",
    "D": "double",
    "F": "float",
    "J": "s64",
    "I": "s32"
}

union_types = {
    "V": "<inval>",
    "D": "d",
    "F": "f",
    "J": "l",
    "I": "i"
}

def gen_trampoline(return_type, arguments, _freq):
    sl = wasm_types[return_type] + "".join([wasm_types[arg] for arg in arguments])
    old_args = arguments
    name = return_type + arguments
    arguments = ", ".join([c_types[arg] for arg in arguments])
    assert(not any([arg == "V" for arg in arguments.split(", ")]))

    the_args = ""
    if len(arguments):
        the_args = ", "
    print(f"static {c_types[return_type]} jit_tramp_{name}({c_types[return_type]} (*f)(vm_thread *, cp_method *{the_args} {arguments}), vm_thread *thread, cp_method *method, stack_value *args) {{")

    maybe_return = "return" if return_type != "V" else ""
    print(f"  MUSTTAIL {maybe_return} f(thread, method{the_args}{', '.join([f"args[{i}].{union_types[arg]}" for i, arg in enumerate(old_args)])});")
    print("}")

    blah = ", ".join([f"{c_types[arg]} arg{i}" for i, arg in enumerate(old_args)])

    print(f"static {c_types[return_type]} interpreter_tramp_{name}(vm_thread *thread, cp_method *method{the_args} {blah}) {{")
    print(f"  stack_value values[{len(arguments)}];")
    for i, arg in enumerate(old_args):
        print(f"  values[{i}].{union_types[arg]} = arg{i};")
    print(f"  [[maybe_unused]] stack_value result = call_interpreter_synchronous(thread, method, values);")
    if return_type != "V":
        print(f"  return result.{union_types[return_type]};")
    print("}")

    return (name, sl)

names = [gen_trampoline(*d) for d in desired]

def emit_add_to_hash_tbl(names):
    adds = []
    for (name, sl) in names:
        adds.append(f"  (void)hash_table_insert(&jit_trampolines, \"{sl}\", {len(name)}, (void *) jit_tramp_{name});")
        adds.append(f"  (void)hash_table_insert(&interpreter_trampolines, \"{sl}\", {len(name)}, (void *) interpreter_tramp_{name});")
    print(f"""
static void add_pregenerated_trampolines() {{
{"\n".join(adds)}
}}
""")

emit_add_to_hash_tbl(names)