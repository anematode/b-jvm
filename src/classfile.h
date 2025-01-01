//
// Created by alec on 12/18/24.
//

#ifndef BJVM_CLASSFILE_H
#define BJVM_CLASSFILE_H

#include "adt.h"
#include "util.h"
#include "vtable.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bjvm_cp_entry bjvm_cp_entry;
typedef struct bjvm_field_descriptor bjvm_field_descriptor;

bjvm_cp_entry *bjvm_check_cp_entry(bjvm_cp_entry *entry, int expected_kinds,
                                   const char *reason);

char *parse_field_descriptor(const char **chars, size_t len,
                             bjvm_field_descriptor *result);

/**
 * Instruction code. Similar instructions like aload_0 are canonicalised to
 * aload with an argument of 0.
 *
 * The order of the instructions matters.
 *
 * List of canonicalisations:
 *   aload_<n> -> aload, dload_<n> -> dload, fload_<n> -> fload, iload_<n> ->
 * iload, lload_<n> -> lload, astore_<n> -> astore, dstore_<n> -> dstore,
 * fstore_<n> -> fstore, istore_<n> -> istore, lstore_<n> -> lstore, bipush,
 * sipush, iconst_<n>, iconst_<n> -> iconst, dconst_<d> -> dconst, fconst_<f> ->
 * fconst
 */
typedef enum {
  /** No operands */
  bjvm_insn_nop,

  bjvm_insn_aaload,
  bjvm_insn_aastore,
  bjvm_insn_aconst_null,
  bjvm_insn_areturn,
  bjvm_insn_arraylength,
  bjvm_insn_athrow,
  bjvm_insn_baload,
  bjvm_insn_bastore,
  bjvm_insn_caload,
  bjvm_insn_castore,
  bjvm_insn_d2f,
  bjvm_insn_d2i,
  bjvm_insn_d2l,
  bjvm_insn_dadd,
  bjvm_insn_daload,
  bjvm_insn_dastore,
  bjvm_insn_dcmpg,
  bjvm_insn_dcmpl,
  bjvm_insn_ddiv,
  bjvm_insn_dmul,
  bjvm_insn_dneg,
  bjvm_insn_drem,
  bjvm_insn_dreturn,
  bjvm_insn_dsub,
  bjvm_insn_dup,
  bjvm_insn_dup_x1,
  bjvm_insn_dup_x2,
  bjvm_insn_dup2,
  bjvm_insn_dup2_x1,
  bjvm_insn_dup2_x2,
  bjvm_insn_f2d,
  bjvm_insn_f2i,
  bjvm_insn_f2l,
  bjvm_insn_fadd,
  bjvm_insn_faload,
  bjvm_insn_fastore,
  bjvm_insn_fcmpg,
  bjvm_insn_fcmpl,
  bjvm_insn_fdiv,
  bjvm_insn_fmul,
  bjvm_insn_fneg,
  bjvm_insn_frem,
  bjvm_insn_freturn,
  bjvm_insn_fsub,
  bjvm_insn_i2b,
  bjvm_insn_i2c,
  bjvm_insn_i2d,
  bjvm_insn_i2f,
  bjvm_insn_i2l,
  bjvm_insn_i2s,
  bjvm_insn_iadd,
  bjvm_insn_iaload,
  bjvm_insn_iand,
  bjvm_insn_iastore,
  bjvm_insn_idiv,
  bjvm_insn_imul,
  bjvm_insn_ineg,
  bjvm_insn_ior,
  bjvm_insn_irem,
  bjvm_insn_ireturn,
  bjvm_insn_ishl,
  bjvm_insn_ishr,
  bjvm_insn_isub,
  bjvm_insn_iushr,
  bjvm_insn_ixor,
  bjvm_insn_l2d,
  bjvm_insn_l2f,
  bjvm_insn_l2i,
  bjvm_insn_ladd,
  bjvm_insn_laload,
  bjvm_insn_land,
  bjvm_insn_lastore,
  bjvm_insn_lcmp,
  bjvm_insn_ldiv,
  bjvm_insn_lmul,
  bjvm_insn_lneg,
  bjvm_insn_lor,
  bjvm_insn_lrem,
  bjvm_insn_lreturn,
  bjvm_insn_lshl,
  bjvm_insn_lshr,
  bjvm_insn_lsub,
  bjvm_insn_lushr,
  bjvm_insn_lxor,
  bjvm_insn_monitorenter,
  bjvm_insn_monitorexit,
  bjvm_insn_pop,
  bjvm_insn_pop2,
  bjvm_insn_return,
  bjvm_insn_saload,
  bjvm_insn_sastore,
  bjvm_insn_swap,

  /** Indexes into constant pool */
  bjvm_insn_anewarray,
  bjvm_insn_checkcast,
  bjvm_insn_getfield,
  bjvm_insn_getstatic,
  bjvm_insn_instanceof,
  bjvm_insn_invokedynamic,
  bjvm_insn_new,
  bjvm_insn_putfield,
  bjvm_insn_putstatic,
  bjvm_insn_invokevirtual,
  bjvm_insn_invokespecial,
  bjvm_insn_invokestatic,
  bjvm_insn_ldc,
  bjvm_insn_ldc2_w,

  /** Indexes into local variable table */
  bjvm_insn_dload,
  bjvm_insn_fload,
  bjvm_insn_iload,
  bjvm_insn_lload,
  bjvm_insn_dstore,
  bjvm_insn_fstore,
  bjvm_insn_istore,
  bjvm_insn_lstore,
  bjvm_insn_aload,
  bjvm_insn_astore,

  /** Indexes into instruction table */
  bjvm_insn_goto,
  bjvm_insn_jsr,

  bjvm_insn_if_acmpeq,
  bjvm_insn_if_acmpne,
  bjvm_insn_if_icmpeq,
  bjvm_insn_if_icmpne,
  bjvm_insn_if_icmplt,
  bjvm_insn_if_icmpge,
  bjvm_insn_if_icmpgt,
  bjvm_insn_if_icmple,
  bjvm_insn_ifeq,
  bjvm_insn_ifne,
  bjvm_insn_iflt,
  bjvm_insn_ifge,
  bjvm_insn_ifgt,
  bjvm_insn_ifle,
  bjvm_insn_ifnonnull,
  bjvm_insn_ifnull,

  /** Has some numerical immediate */
  bjvm_insn_iconst,
  bjvm_insn_dconst,
  bjvm_insn_fconst,
  bjvm_insn_lconst,

  /** Cursed */
  bjvm_insn_iinc,
  bjvm_insn_invokeinterface,
  bjvm_insn_multianewarray,
  bjvm_insn_newarray,
  bjvm_insn_tableswitch,
  bjvm_insn_lookupswitch,
  bjvm_insn_ret,

  /** Resolved versions of invoke* */
  bjvm_insn_invokevtable_monomorphic,  // inline cache with previous object
  bjvm_insn_invokevtable_polymorphic,  // slower dispatch
  bjvm_insn_invokeitable_monomorphic,  // inline cache with previous object
  bjvm_insn_invokeitable_polymorphic,  // slower dispatch
  bjvm_insn_invokecallsite,  // resolved version of invokedynamic
  bjvm_insn_invokestatic_resolved,
  bjvm_insn_invokespecial_resolved
} bjvm_insn_code_kind;

typedef enum : char {
  BJVM_TYPE_KIND_BOOLEAN = 'Z',
  BJVM_TYPE_KIND_CHAR = 'C',
  BJVM_TYPE_KIND_FLOAT = 'F',
  BJVM_TYPE_KIND_DOUBLE = 'D',
  BJVM_TYPE_KIND_BYTE = 'B',
  BJVM_TYPE_KIND_SHORT = 'S',
  BJVM_TYPE_KIND_INT = 'I',
  BJVM_TYPE_KIND_LONG = 'J',
  BJVM_TYPE_KIND_VOID = 'V',
  BJVM_TYPE_KIND_REFERENCE = 'L'
} bjvm_type_kind;

typedef enum {
  BJVM_CD_KIND_ORDINARY,
  // e.g. classdesc corresponding to int.class. No objects mapping to this
  // classdesc are actually constructed.
  BJVM_CD_KIND_PRIMITIVE,
  BJVM_CD_KIND_ORDINARY_ARRAY,
  // e.g. [Z, [[[J  (i.e., multidimensional arrays are counted here)
  BJVM_CD_KIND_PRIMITIVE_ARRAY,
} bjvm_classdesc_kind;

typedef enum {
  BJVM_CD_STATE_LINKAGE_ERROR = 0,
  BJVM_CD_STATE_LOADED = 1,
  BJVM_CD_STATE_LINKED = 2,
  BJVM_CD_STATE_INITIALIZING = 3,
  BJVM_CD_STATE_INITIALIZED = 4
} bjvm_classdesc_state;

typedef struct bjvm_classdesc bjvm_classdesc;
typedef struct bjvm_bootstrap_method bjvm_bootstrap_method;

typedef struct {
  bjvm_classdesc *classdesc;
  bjvm_utf8 name;

  void *resolution_error; // bjvm_obj_header
} bjvm_cp_class_info;

typedef struct bjvm_cp_name_and_type {
  bjvm_utf8 name;
  bjvm_utf8 descriptor;
} bjvm_cp_name_and_type;

struct bjvm_field_descriptor {
  bjvm_type_kind base_kind;
  // Can be nonzero for any kind
  int dimensions;
  heap_string class_name; // For reference and array types only
};

typedef struct bjvm_cp_field bjvm_cp_field;

typedef struct {
  bjvm_cp_class_info *class_info;
  bjvm_cp_name_and_type *nat;

  bjvm_field_descriptor *parsed_descriptor;
  bjvm_cp_field *field;
} bjvm_cp_field_info;

typedef struct bjvm_method_descriptor bjvm_method_descriptor;

// Used by both methodref and interface methodref
typedef struct {
  bjvm_cp_class_info *class_info;
  bjvm_cp_name_and_type *nat;
  bjvm_method_descriptor *descriptor;

  // The resolved method -- initially nullptr
  bjvm_cp_method *resolved;
} bjvm_cp_method_info;

typedef struct {
  bjvm_utf8 chars;
} bjvm_cp_string_info;

typedef struct {
  // Sign-extended if original entry was an Integer
  int64_t value;
} bjvm_cp_integral_info;

typedef struct {
  // Value-extended if original entry was a Float
  double value;
} bjvm_cp_floating_info;

typedef enum {
  BJVM_MH_KIND_GET_FIELD = 1,
  BJVM_MH_KIND_GET_STATIC = 2,
  BJVM_MH_KIND_PUT_FIELD = 3,
  BJVM_MH_KIND_PUT_STATIC = 4,
  BJVM_MH_KIND_INVOKE_VIRTUAL = 5,
  BJVM_MH_KIND_INVOKE_STATIC = 6,
  BJVM_MH_KIND_INVOKE_SPECIAL = 7,
  BJVM_MH_KIND_NEW_INVOKE_SPECIAL = 8,
  BJVM_MH_KIND_INVOKE_INTERFACE = 9
} bjvm_method_handle_kind;

typedef struct {
  bjvm_method_handle_kind handle_kind;
  bjvm_cp_entry *reference;

  struct bjvm_native_MethodType *resolved_mt;
} bjvm_cp_method_handle_info;

typedef struct {
  bjvm_utf8 descriptor;
  bjvm_method_descriptor *parsed_descriptor;

  struct bjvm_native_MethodType *resolved_mt;
} bjvm_cp_method_type_info;

typedef struct {
  bjvm_bootstrap_method *method;
  bjvm_cp_name_and_type *name_and_type;
  bjvm_method_descriptor *method_descriptor;

  struct bjvm_native_MethodType *resolved_mt;
} bjvm_cp_indy_info;

typedef enum {
  BJVM_CP_KIND_INVALID = 0,
  BJVM_CP_KIND_UTF8 = 1 << 0,
  BJVM_CP_KIND_INTEGER = 1 << 1,
  BJVM_CP_KIND_FLOAT = 1 << 2,
  BJVM_CP_KIND_LONG = 1 << 3,
  BJVM_CP_KIND_DOUBLE = 1 << 4,
  BJVM_CP_KIND_CLASS = 1 << 5,
  BJVM_CP_KIND_STRING = 1 << 6,
  BJVM_CP_KIND_FIELD_REF = 1 << 7,
  BJVM_CP_KIND_METHOD_REF = 1 << 8,
  BJVM_CP_KIND_INTERFACE_METHOD_REF = 1 << 9,
  BJVM_CP_KIND_NAME_AND_TYPE = 1 << 10,
  BJVM_CP_KIND_METHOD_HANDLE = 1 << 11,
  BJVM_CP_KIND_METHOD_TYPE = 1 << 12,
  BJVM_CP_KIND_INVOKE_DYNAMIC = 1 << 13,
} bjvm_cp_kind;

typedef struct bjvm_cp_entry {
  bjvm_cp_kind kind;
  // Index of this entry within the constant pool
  int my_index;

  union {
    heap_string utf8;
    bjvm_cp_string_info string;

    bjvm_cp_floating_info floating;
    bjvm_cp_integral_info integral;

    bjvm_cp_name_and_type name_and_type;
    bjvm_cp_class_info class_info;

    bjvm_cp_field_info field;
    bjvm_cp_method_info methodref;
    bjvm_cp_method_handle_info method_handle;
    bjvm_cp_method_type_info method_type;
    bjvm_cp_indy_info indy_info;
  };
} bjvm_cp_entry;

struct bjvm_multianewarray_data {
  bjvm_cp_class_info *entry;
  uint8_t dimensions;
};

typedef struct bjvm_constant_pool {
  int entries_len;
  bjvm_cp_entry entries[];
} bjvm_constant_pool;

// Access flags. Same as given in the class file specification.
typedef enum {
  BJVM_ACCESS_PUBLIC = 0x0001,
  BJVM_ACCESS_PRIVATE = 0x0002,
  BJVM_ACCESS_PROTECTED = 0x0004,
  BJVM_ACCESS_STATIC = 0x0008,
  BJVM_ACCESS_FINAL = 0x0010,
  BJVM_ACCESS_SYNCHRONIZED = 0x0020,
  BJVM_ACCESS_BRIDGE = 0x0040,
  BJVM_ACCESS_VARARGS = 0x0080,
  BJVM_ACCESS_NATIVE = 0x0100,
  BJVM_ACCESS_INTERFACE = 0x0200,
  BJVM_ACCESS_ABSTRACT = 0x0400,
  BJVM_ACCESS_STRICT = 0x0800,
  BJVM_ACCESS_SYNTHETIC = 0x1000,
  BJVM_ACCESS_ANNOTATION = 0x2000,
  BJVM_ACCESS_ENUM = 0x4000
} bjvm_access_flags;

typedef enum {
  BJVM_ATTRIBUTE_KIND_CODE,
  BJVM_ATTRIBUTE_KIND_CONSTANT_VALUE,
  BJVM_ATTRIBUTE_KIND_UNKNOWN,
  BJVM_ATTRIBUTE_KIND_BOOTSTRAP_METHODS,
  BJVM_ATTRIBUTE_KIND_ENCLOSING_METHOD,
  BJVM_ATTRIBUTE_KIND_SOURCE_FILE,
  BJVM_ATTRIBUTE_KIND_LINE_NUMBER_TABLE,
  BJVM_ATTRIBUTE_KIND_METHOD_PARAMETERS,
  BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS,
  BJVM_ATTRIBUTE_KIND_SIGNATURE,
  BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS,
  BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_TYPE_ANNOTATIONS,
  BJVM_ATTRIBUTE_KIND_ANNOTATION_DEFAULT
} bjvm_attribute_kind;

typedef struct bjvm_method_descriptor {
  bjvm_field_descriptor *args;
  int args_count;
  int args_cap;
  bjvm_field_descriptor return_type;
} bjvm_method_descriptor;

typedef struct {
  int start_insn;
  int end_insn;
  int handler_insn;
  bjvm_cp_class_info *catch_type;
} bjvm_exception_table_entry;

typedef struct {
  bjvm_exception_table_entry *entries;
  int entries_count;
} bjvm_attribute_exception_table;

typedef struct bjvm_attribute bjvm_attribute;

typedef struct {
  int start_pc;
  int line;
} bjvm_line_number_table_entry;

typedef struct {
  bjvm_line_number_table_entry *entries;
  int entry_count;
} bjvm_attribute_line_number_table;

typedef struct {
  bjvm_utf8 name;
  uint16_t access_flags;
} bjvm_method_parameter_info;

typedef struct {
  bjvm_method_parameter_info *params;
  int count;
} bjvm_attribute_method_parameters;

struct bjvm_bc_tableswitch_data {
  // Note: If changing this, make sure the layout of the first three fields
  // is the same as bjvm_bc_lookupswitch_data.
  int default_target;
  int *targets;
  int targets_count;

  int low, high;
};

struct bjvm_bc_lookupswitch_data {
  // Note: If changing this, make sure the layout of the first three fields
  // is the same as bjvm_bc_tableswitch_data.
  int default_target;
  int *targets;
  int targets_count;

  int *keys;
  int keys_count;
};

struct bjvm_bc_iinc_data {
  uint16_t index;
  int16_t const_;
};

struct bjvm_bc_invokeinterface_data {
  uint16_t index;
  uint8_t count;
};

typedef struct bjvm_bytecode_insn {
  bjvm_insn_code_kind kind;
  int original_pc;

  union {
    // for newarray
    bjvm_type_kind array_type;
    // constant pool index or local variable index or branch target (instruction
    // index)
    uint32_t index;
    // Integer or long immediate
    int64_t integer_imm;
    // Float immediate
    float f_imm;
    // Double immediate
    double d_imm;
    // lookupswitch
    struct bjvm_bc_lookupswitch_data lookupswitch;
    // tableswitch
    struct bjvm_bc_tableswitch_data tableswitch;
    // iinc
    struct bjvm_bc_iinc_data iinc;
    // invoke interface
    struct bjvm_bc_invokeinterface_data invokeinterface;
    // multianewarray
    struct bjvm_multianewarray_data multianewarray;
    // non-owned pointer into the constant pool
    bjvm_cp_entry *cp;
  };
  // Per-instruction inline cache data
  void *ic;
  void *ic2;
  int args;
  int table_index;
} bjvm_bytecode_insn;

typedef struct {
  uint16_t max_stack;
  uint16_t max_locals;
  int insn_count;
  int max_formal_pc;

  bjvm_bytecode_insn *code;
  bjvm_attribute_exception_table *exception_table;
  bjvm_attribute_line_number_table *line_number_table;

  bjvm_attribute *attributes;
  int attributes_count;
} bjvm_attribute_code;

typedef struct bjvm_bootstrap_method {
  bjvm_cp_method_handle_info *ref;
  bjvm_cp_entry **args;
  int args_count;
} bjvm_bootstrap_method;

typedef struct {
  int count;
  bjvm_bootstrap_method *methods;
} bjvm_attribute_bootstrap_methods;

typedef struct {
  bjvm_cp_class_info *class_info;
  bjvm_cp_name_and_type *nat;
} bjvm_attribute_enclosing_method;

typedef struct {
  uint8_t *data;
  int length;
} bjvm_attribute_runtime_visible_annotations;

typedef struct {
  uint8_t *data;
  int length;
} bjvm_attribute_runtime_visible_parameter_annotations;

typedef struct {
  uint8_t *data;
  int length;
} bjvm_attribute_runtime_visible_type_annotations;

typedef struct {
  uint8_t *data;
  int length;
} bjvm_attribute_annotation_default;

typedef struct {
  bjvm_utf8 utf8;
} bjvm_attribute_signature;

typedef struct {
  bjvm_utf8 name;
} bjvm_attribute_source_file;

typedef struct bjvm_attribute {
  bjvm_attribute_kind kind;
  bjvm_utf8 name;
  uint32_t length;

  union {
    bjvm_attribute_code code;
    bjvm_cp_entry *constant_value;
    bjvm_attribute_bootstrap_methods bootstrap_methods;
    bjvm_attribute_enclosing_method enclosing_method;
    bjvm_attribute_source_file source_file;
    bjvm_attribute_line_number_table lnt;
    bjvm_attribute_method_parameters method_parameters;
    bjvm_attribute_runtime_visible_annotations annotations;
    bjvm_attribute_runtime_visible_parameter_annotations parameter_annotations;
    bjvm_attribute_runtime_visible_type_annotations type_annotations;
    bjvm_attribute_annotation_default annotation_default;
    bjvm_attribute_signature signature;
  };
} bjvm_attribute;

typedef struct bjvm_code_analysis bjvm_code_analysis;

typedef struct bjvm_cp_method {
  bjvm_access_flags access_flags;

  bjvm_utf8 name;
  bjvm_utf8 unparsed_descriptor;

  bjvm_method_descriptor *descriptor;
  bjvm_code_analysis *code_analysis;

  int attributes_count;
  bjvm_attribute *attributes;
  bjvm_attribute_code *code;

  bool is_signature_polymorphic;
  bjvm_classdesc *my_class;

  // Index in the vtable, if applicable. Only set at class link time.
  int vtable_index;
  // Index in the itable, if applicable.
  int itable_index;
  void *native_handle; // bjvm_native_callback

  struct bjvm_native_Constructor *reflection_ctor;
  struct bjvm_native_Method *reflection_method;
  struct bjvm_native_MethodType *method_type_obj;

  // Rough number of times this method has been called. Used for JIT heuristics.
  // Not at all exact because of interrupts.
  int call_count;

  // This method overrides a method in a superclass
  bool overrides;
  // This method overrides a method in an interface
  bool overrides_interface;

  // JIT-compiled method
  void *compiled_method; // bjvm_wasm_instantiation_result*
  // Already tried and failed
  bool failed_jit;
} bjvm_cp_method;

typedef struct bjvm_cp_field {
  bjvm_access_flags access_flags;
  bjvm_utf8 name;
  bjvm_utf8 descriptor;

  int attributes_count;
  bjvm_attribute *attributes;
  // Offset of the field in the static or instance data area
  int byte_offset;

  bjvm_field_descriptor parsed_descriptor;
  struct bjvm_native_Field *reflection_field;

  bjvm_classdesc *my_class;
} bjvm_cp_field;

typedef bjvm_utf8 cp_string;

// Class descriptor. (Roughly equivalent to HotSpot's InstanceKlass)
typedef struct bjvm_classdesc {
  bjvm_classdesc_kind kind;
  bjvm_classdesc_state state;
  bjvm_constant_pool *pool;

  bjvm_access_flags access_flags;
  heap_string name;
  bjvm_cp_class_info *super_class;

  int interfaces_count;
  bjvm_cp_class_info **interfaces;

  int fields_count;
  bjvm_cp_field *fields;

  int methods_count;
  bjvm_cp_method *methods;

  bjvm_attribute_bootstrap_methods *bootstrap_methods;
  bjvm_attribute_source_file *source_file;

  int attributes_count;
  bjvm_attribute *attributes;
  bjvm_classdesc *array_type;

  uint8_t *static_fields;
  // Number of bytes (including the object header) which an instance of this
  // class takes up. Unused for array types.
  int instance_bytes;

  struct bjvm_native_Class *mirror;
  struct bjvm_native_ConstantPool *cp_mirror;

  // Non-array classes: which 4- (32-bit system) or 8-byte aligned offsets
  // correspond to references that need to be followed
  bjvm_compressed_bitset static_references;
  bjvm_compressed_bitset instance_references;

  bjvm_classdesc *one_fewer_dim; // NULL for non-array types
  bjvm_classdesc *base_component;

  int dimensions;                     // array types only
  bjvm_type_kind primitive_component; // primitives and array types only

  int indy_insns_count;
  bjvm_bytecode_insn **indy_insns; // used to get GC roots

  void (*dtor)(bjvm_classdesc *); // apoptosis

  bjvm_vtable vtable;
  bjvm_itables itables;
} bjvm_classdesc;

heap_string insn_to_string(const bjvm_bytecode_insn *insn, int insn_index);

typedef enum { PARSE_SUCCESS = 0, PARSE_ERR = 1 } parse_result_t;

/**
 * Parse a Java class file.
 *
 * The error message corresponds to a ClassFormatError in Java land.
 * (UnsupportedClassVersionErrors and VerifyErrors should be raised elsewhere.)
 *
 * @param bytes Start byte of the classfile.
 * @param len Length of the classfile in bytes.
 * @param result Where to write the result.
 * @return nullptr on success, otherwise an error message (which is the caller's
 * responsibility to free).
 */
parse_result_t bjvm_parse_classfile(uint8_t *bytes, size_t len,
                                    bjvm_classdesc *result);

#ifdef __cplusplus
}
#endif

#endif // BJVM_CLASSFILE_H
