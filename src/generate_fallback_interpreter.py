import re
# Associate instruction x and TOS type t with computed goto index 4*x + t.

# Read classfile.h and extract the list of instructions
classfile_h = open("classfile.h", "r").read()
# search for "bjvm_insn_code_kind" and find the matching { BEFORE it
end = classfile_h.find("bjvm_insn_code_kind")
start = classfile_h.rfind("{", 0, end)

lines = classfile_h[start:end].split("\n")
# Filter by bjvm_insn_([a-zA-Z0-9_]+) and extract
instructions = []
for line in lines:
    match = re.search(r"bjvm_insn_([a-zA-Z0-9_]+),\s*($|//)", line)
    if match:
        instructions.append(match.group(1))

outline_insns = [
    "invokevirtual",
    "invokespecial",
    "invokestatic",
    "ldc",
    "invokeinterface",
    "invokevtable_polymorphic",
    "invokeitable_polymorphic",
    "invokecallsite",
    "invokedynamic"
]

def is_return(instruction):
    return ["dreturn", "freturn", "ireturn", "lreturn", "return", "areturn"].count(instruction) > 0

special = {
    "invokeitable_monomorphic": "invokeitable_vtable_monomorphic",
    "invokevtable_monomorphic": "invokeitable_vtable_monomorphic",
}

interpreter2_c = open("interpreter2.c", "r").read()
labels = ""
cases = ""
for i, instruction in enumerate(instructions):
    for tos in ["void", "double", "int", "float"]:
        labels += f"&&{instruction}_{tos},\n"
        exists = (instruction in special) or re.search(rf"\s+{instruction}_impl_{tos},\s*\n", interpreter2_c)

        name = special[instruction] if instruction in special else instruction
        outline = instruction in outline_insns

        int_tos = "int_tos_" if outline else "int_tos"
        float_tos = "float_tos_" if outline else "float_tos"
        double_tos = "double_tos_" if outline else "double_tos"

        outline_begin = "int_tos_ = int_tos; float_tos_ = float_tos; double_tos_ = double_tos;" if outline else ""

        call = f"{name}_impl_{tos}(thread, frame_, &current, &pc_, &sp_, &{int_tos}, &{float_tos}, &{double_tos});"

        outline_end = f"int_tos = int_tos_; float_tos = float_tos_; double_tos = double_tos_;" if outline else ""

        if not exists:
            cases += f"""
{instruction}_{tos}: __builtin_unreachable();
"""
        elif is_return(instruction):
            cases += f"""
{instruction}_{tos}:
    result.l = {call};
    goto done;"""
        else:
            cases += f"""
{instruction}_{tos}: {outline_begin}
    jump_to = (unsigned){call}; {outline_end}
    continue;"""


result = (f"""
static const void *insn_jump_table[] = {{
   {labels}
   &&escape
}};

goto *insn_jump_table[jump_to];

{cases}
escape:
break;
""")

open("interpreter2-computed-goto.inc", "w").write(result)