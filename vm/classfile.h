//
// Created by alec on 12/18/24.
//

#ifndef CLASSFILE_H
#define CLASSFILE_H

#include "adt.h"
#include "util.h"
#include "vtable.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cp_entry cp_entry;
typedef struct field_descriptor field_descriptor;

enum cp_kind : u32;
typedef enum cp_kind cp_kind;

cp_entry *check_cp_entry(cp_entry *entry, cp_kind expected_kinds, const char *reason);

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
typedef enum : u8 {
  /** No operands */
  insn_nop, // stack polymorphic

  insn_aaload,
  insn_aastore,
  insn_aconst_null,
  insn_areturn,
  insn_arraylength,
  insn_athrow,
  insn_baload,
  insn_bastore,
  insn_caload,
  insn_castore,
  insn_d2f,
  insn_d2i,
  insn_d2l,
  insn_dadd,
  insn_daload,
  insn_dastore,
  insn_dcmpg,
  insn_dcmpl,
  insn_ddiv,
  insn_dmul,
  insn_dneg,
  insn_drem,
  insn_dreturn,
  insn_dsub,
  insn_dup,
  insn_dup_x1,
  insn_dup_x2,
  insn_dup2,
  insn_dup2_x1,
  insn_dup2_x2,
  insn_f2d,
  insn_f2i,
  insn_f2l,
  insn_fadd,
  insn_faload,
  insn_fastore,
  insn_fcmpg,
  insn_fcmpl,
  insn_fdiv,
  insn_fmul,
  insn_fneg,
  insn_frem,
  insn_freturn,
  insn_fsub,
  insn_i2b,
  insn_i2c,
  insn_i2d,
  insn_i2f,
  insn_i2l,
  insn_i2s,
  insn_iadd,
  insn_iaload,
  insn_iand,
  insn_iastore,
  insn_idiv,
  insn_imul,
  insn_ineg,
  insn_ior,
  insn_irem,
  insn_ireturn,
  insn_ishl,
  insn_ishr,
  insn_isub,
  insn_iushr,
  insn_ixor,
  insn_l2d,
  insn_l2f,
  insn_l2i,
  insn_ladd,
  insn_laload,
  insn_land,
  insn_lastore,
  insn_lcmp,
  insn_ldiv,
  insn_lmul,
  insn_lneg,
  insn_lor,
  insn_lrem,
  insn_lreturn,
  insn_lshl,
  insn_lshr,
  insn_lsub,
  insn_lushr,
  insn_lxor,
  insn_monitorenter,
  insn_monitorexit,
  insn_pop,
  insn_pop2,
  insn_return,
  insn_saload,
  insn_sastore,
  insn_swap,

  /** Indexes into constant pool */
  insn_anewarray,
  insn_checkcast,
  insn_getfield,
  insn_getstatic,
  insn_instanceof,
  insn_invokedynamic,
  insn_new,
  insn_putfield,
  insn_putstatic,
  insn_invokevirtual,
  insn_invokespecial,
  insn_invokestatic,
  insn_ldc,
  insn_ldc2_w,

  /** Indexes into local variable table */
  insn_dload,
  insn_fload,
  insn_iload,
  insn_lload,
  insn_dstore,
  insn_fstore,
  insn_istore,
  insn_lstore,
  insn_aload,
  insn_astore,

  /** Indexes into instruction table */
  insn_goto,
  insn_jsr,

  insn_if_acmpeq,
  insn_if_acmpne,
  insn_if_icmpeq,
  insn_if_icmpne,
  insn_if_icmplt,
  insn_if_icmpge,
  insn_if_icmpgt,
  insn_if_icmple,
  insn_ifeq,
  insn_ifne,
  insn_iflt,
  insn_ifge,
  insn_ifgt,
  insn_ifle,
  insn_ifnonnull,
  insn_ifnull,

  /** Has some numerical immediate */
  insn_iconst,
  insn_dconst,
  insn_fconst,
  insn_lconst,

  /** Cursed */
  insn_iinc,
  insn_invokeinterface,
  insn_multianewarray,
  insn_newarray,
  insn_tableswitch,
  insn_lookupswitch,
  insn_ret,

  /** Instruction failed with a LinkageError */
  // insn_linkage_error,

  /** Resolved versions of misc instructions */
  insn_anewarray_resolved,
  insn_checkcast_resolved,
  insn_instanceof_resolved,
  insn_new_resolved,

  /** Resolved versions of invoke* */
  insn_invokevtable_monomorphic, // inline cache with previous object
  insn_invokevtable_polymorphic, // slower vtable-based dispatch
  insn_invokeitable_monomorphic, // inline cache with previous object
  insn_invokeitable_polymorphic, // slower itable-based dispatch
  insn_invokespecial_resolved,   // resolved version of invokespecial
  insn_invokestatic_resolved,    // resolved version of invokestatic
  insn_invokecallsite,           // resolved version of invokedynamic
  insn_invokesigpoly,

  /** Resolved versions of getfield */
  insn_getfield_B,
  insn_getfield_C,
  insn_getfield_S,
  insn_getfield_I,
  insn_getfield_J,
  insn_getfield_F,
  insn_getfield_D,
  insn_getfield_Z,
  insn_getfield_L,

  /** Resolved versions of putfield */
  insn_putfield_B,
  insn_putfield_C,
  insn_putfield_S,
  insn_putfield_I,
  insn_putfield_J,
  insn_putfield_F,
  insn_putfield_D,
  insn_putfield_Z,
  insn_putfield_L,

  /** Resolved versions of getstatic */
  insn_getstatic_B,
  insn_getstatic_C,
  insn_getstatic_S,
  insn_getstatic_I,
  insn_getstatic_J,
  insn_getstatic_F,
  insn_getstatic_D,
  insn_getstatic_Z,
  insn_getstatic_L,

  /** Resolved versions of putstatic */
  insn_putstatic_B,
  insn_putstatic_C,
  insn_putstatic_S,
  insn_putstatic_I,
  insn_putstatic_J,
  insn_putstatic_F,
  insn_putstatic_D,
  insn_putstatic_Z,
  insn_putstatic_L,

  /** intrinsics understood by the interpreter */
  insn_pow,  // (FF)F, (DD)D
  insn_sin,  // (F)F, (D)D
  insn_cos,  // (F)F, (D)D
  insn_tan,  // (F)F, (D)D
  insn_sqrt  // (F)F, (D)D
} insn_code_kind;

#define MAX_INSN_KIND (insn_sqrt + 1)

// The four top-of-stack kinds considered by the interpreter. (All integer types, including long and reference, are
// merged into one.)
typedef enum : u8 {
  TOS_VOID = 0,
  TOS_DOUBLE = 1,
  TOS_INT = 2,
  TOS_FLOAT = 3,
} reduced_tos_kind;

// type_kind_to_char relies on the order here
typedef enum : char {
  TYPE_KIND_BOOLEAN,
  TYPE_KIND_CHAR,
  TYPE_KIND_FLOAT,
  TYPE_KIND_DOUBLE,
  TYPE_KIND_BYTE,
  TYPE_KIND_SHORT,
  TYPE_KIND_INT,
  TYPE_KIND_LONG,
  TYPE_KIND_VOID,
  TYPE_KIND_REFERENCE
} type_kind;

type_kind read_type_kind_char(char c);
char type_kind_to_char(type_kind kind);

typedef enum {
  CD_KIND_ORDINARY,
  // e.g. classdesc corresponding to int.class. No objects mapping to this
  // classdesc are actually constructed.
  CD_KIND_PRIMITIVE,
  CD_KIND_ORDINARY_ARRAY,
  // e.g. [Z, [[[J  (i.e., multidimensional arrays are counted here)
  CD_KIND_PRIMITIVE_ARRAY,
} classdesc_kind;

typedef enum {
  CD_STATE_LINKAGE_ERROR = 0,
  CD_STATE_LOADED = 1,
  CD_STATE_LINKED = 2,
  CD_STATE_INITIALIZING = 3,
  CD_STATE_INITIALIZED = 4
} classdesc_state;

typedef struct classdesc classdesc;
typedef struct bootstrap_method bootstrap_method;

typedef struct {
  classdesc *classdesc;
  slice name;

  void *vm_object; // linkage error (todo) or resolved class
} cp_class_info;

typedef struct cp_name_and_type {
  slice name;
  slice descriptor;
} cp_name_and_type;

struct field_descriptor {
  // Base kind (e.g. B for [B, I for [[I, Ljava/lang/String; for Ljava/lang/String;)
  type_kind base_kind;
  // Representable kind (e.g. REFERENCE for [B
  type_kind repr_kind;
  // Can be nonzero for any kind
  int dimensions;
  slice class_name; // For reference and array types only
  slice unparsed;
};

typedef struct cp_field cp_field;

typedef struct {
  cp_class_info *class_info;
  cp_name_and_type *nat;

  field_descriptor *parsed_descriptor;
  cp_field *field;
} cp_field_info;

typedef struct method_descriptor method_descriptor;

// Used by both methodref and interface methodref
typedef struct {
  cp_class_info *class_info;
  cp_name_and_type *nat;
  method_descriptor *descriptor;

  // The resolved method -- initially nullptr
  cp_method *resolved;
} cp_method_info;

typedef struct {
  slice chars;
  void *interned; // pointer to the interned string, if instantiated
} cp_string_info;

typedef struct {
  // Sign-extended if original entry was an Integer
  s64 value;
} cp_integral_info;

typedef struct {
  // Value-extended if original entry was a Float
  double value;
} cp_floating_info;

typedef enum {
  MH_KIND_GET_FIELD = 1,
  MH_KIND_GET_STATIC = 2,
  MH_KIND_PUT_FIELD = 3,
  MH_KIND_PUT_STATIC = 4,
  MH_KIND_INVOKE_VIRTUAL = 5,
  MH_KIND_INVOKE_STATIC = 6,
  MH_KIND_INVOKE_SPECIAL = 7,
  MH_KIND_NEW_INVOKE_SPECIAL = 8,
  MH_KIND_INVOKE_INTERFACE = 9,
  MH_KIND_LAST = 9
} method_handle_kind;

static inline bool mh_is_invoke(method_handle_kind kind) {
  return kind >= MH_KIND_INVOKE_VIRTUAL && kind <= MH_KIND_INVOKE_INTERFACE;
}

static inline bool mh_is_vh(method_handle_kind kind) {
  // return kind & (MH_KIND_GET_FIELD | MH_KIND_GET_STATIC |
  //                MH_KIND_PUT_FIELD | MH_KIND_PUT_STATIC);
  return kind <= MH_KIND_PUT_STATIC && kind >= MH_KIND_GET_FIELD;
}

typedef struct {
  method_handle_kind handle_kind;
  cp_entry *reference;

  struct native_MethodType *resolved_mt;
} cp_method_handle_info;

typedef struct {
  slice descriptor;
  method_descriptor *parsed_descriptor;

  struct native_MethodType *resolved_mt;
} cp_method_type_info;

typedef struct {
  bootstrap_method *method;
  cp_name_and_type *name_and_type;
  method_descriptor *method_descriptor;

  struct native_MethodType *resolved_mt;
} cp_indy_info;

enum cp_kind : u32 {
  // Bitset so that we can have masks for valid cp kinds
  CP_KIND_INVALID = 0,
  CP_KIND_UTF8 = 1 << 0,
  CP_KIND_INTEGER = 1 << 1,
  CP_KIND_FLOAT = 1 << 2,
  CP_KIND_LONG = 1 << 3,
  CP_KIND_DOUBLE = 1 << 4,
  CP_KIND_CLASS = 1 << 5,
  CP_KIND_STRING = 1 << 6,
  CP_KIND_FIELD_REF = 1 << 7,
  CP_KIND_METHOD_REF = 1 << 8,
  CP_KIND_INTERFACE_METHOD_REF = 1 << 9,
  CP_KIND_NAME_AND_TYPE = 1 << 10,
  CP_KIND_METHOD_HANDLE = 1 << 11,
  CP_KIND_METHOD_TYPE = 1 << 12,
  CP_KIND_INVOKE_DYNAMIC = 1 << 13,
  CP_KIND_DYNAMIC_CONSTANT = 1 << 14,
  CP_KIND_MODULE = 1 << 15,
  CP_KIND_PACKAGE = 1 << 16,
  CP_KIND_LAST = 1 << 16
};

static inline bool cp_kind_is_primitive(cp_kind kind) {
  return kind == CP_KIND_INTEGER || kind == CP_KIND_FLOAT || kind == CP_KIND_LONG || kind == CP_KIND_DOUBLE;
}

typedef struct cp_entry {
  cp_kind kind;
  // Index of this entry within the constant pool
  int my_index;

  union {
    slice utf8;
    cp_string_info string;

    cp_floating_info floating;
    cp_integral_info integral;

    cp_name_and_type name_and_type;
    cp_class_info class_info;

    cp_field_info field;
    cp_method_info methodref;
    cp_method_handle_info method_handle;
    cp_method_type_info method_type;
    cp_indy_info indy_info;
  };
} cp_entry;

struct multianewarray_data {
  cp_class_info *entry;
  u8 dimensions;
};

typedef struct constant_pool {
  int entries_len;
  cp_entry entries[];
} constant_pool;

// Access flags. Same as given in the class file specification.
typedef enum : u16 {
  ACCESS_PUBLIC = 0x0001,
  ACCESS_PRIVATE = 0x0002,
  ACCESS_PROTECTED = 0x0004,
  ACCESS_STATIC = 0x0008,
  ACCESS_FINAL = 0x0010,
  ACCESS_SYNCHRONIZED = 0x0020,
  ACCESS_BRIDGE = 0x0040,
  ACCESS_VARARGS = 0x0080,
  ACCESS_NATIVE = 0x0100,
  ACCESS_INTERFACE = 0x0200,
  ACCESS_ABSTRACT = 0x0400,
  ACCESS_STRICT = 0x0800,
  ACCESS_SYNTHETIC = 0x1000,
  ACCESS_ANNOTATION = 0x2000,
  ACCESS_ENUM = 0x4000,
  ACCESS_MODULE = 0x8000
} access_flags;

typedef enum {
  ATTRIBUTE_KIND_CODE,
  ATTRIBUTE_KIND_CONSTANT_VALUE,
  ATTRIBUTE_KIND_UNKNOWN,
  ATTRIBUTE_KIND_BOOTSTRAP_METHODS,
  ATTRIBUTE_KIND_ENCLOSING_METHOD,
  ATTRIBUTE_KIND_SOURCE_FILE,
  ATTRIBUTE_KIND_LINE_NUMBER_TABLE,
  ATTRIBUTE_KIND_METHOD_PARAMETERS,
  ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS,
  ATTRIBUTE_KIND_SIGNATURE,
  ATTRIBUTE_KIND_RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS,
  ATTRIBUTE_KIND_RUNTIME_VISIBLE_TYPE_ANNOTATIONS,
  ATTRIBUTE_KIND_ANNOTATION_DEFAULT,
  ATTRIBUTE_KIND_NEST_HOST,
  ATTRIBUTE_KIND_LOCAL_VARIABLE_TABLE,
  ATTRIBUTE_KIND_STACK_MAP_TABLE,
  ATTRIBUTE_KIND_INNER_CLASSES,
  ATTRIBUTE_KIND_PERMITTED_SUBCLASSES,
} attribute_kind;

typedef struct method_descriptor {
  field_descriptor *args;
  int args_count;
  field_descriptor return_type;
} method_descriptor;

typedef struct {
  int start_insn;
  int end_insn;
  int handler_insn;
  cp_class_info *catch_type;
} exception_table_entry;

typedef struct {
  exception_table_entry *entries;
  u16 entries_count;
} attribute_exception_table;

typedef struct attribute attribute;

typedef struct {
  int start_pc;
  int line;
} line_number_table_entry;

typedef struct {
  line_number_table_entry *entries;
  int entry_count;
} attribute_line_number_table;

typedef struct {
  int start_pc, end_pc;
  int index;
  slice name;
  slice descriptor;
} attribute_lvt_entry;

// Local variable table
typedef struct {
  attribute_lvt_entry *entries;
  int entries_count;
} attribute_local_variable_table;

// Look up an entry in the local variable table, according to a swizzled local index but the original instruction
// program counter.
const slice *lvt_lookup(int index, int original_pc, const attribute_local_variable_table *table);

typedef struct {
  slice name;
  u16 access_flags;
} method_parameter_info;

typedef struct {
  method_parameter_info *params;
  int count;
} attribute_method_parameters;

struct tableswitch_data {
  // Note: If changing this, make sure the layout of the first three fields
  // is the same as bc_lookupswitch_data.
  int default_target;
  int targets_count;

  int low, high;
  int targets[];
};

struct lookupswitch_data {
  // Note: If changing this, make sure the layout of the first three fields
  // is the same as bc_tableswitch_data.
  int default_target;
  int *targets;
  int targets_count;

  int *keys;
  int keys_count;
};

struct iinc_data {
  u16 index;
  s16 const_;
};

typedef struct bytecode_insn {
  // todo: fix rewriter as the offsets of these things have changed
  insn_code_kind kind;
  u8 args;
  reduced_tos_kind tos_before; // the (reduced) top-of-stack type before this instruction executes
  reduced_tos_kind tos_after;  // the (reduced) top-of-stack type after this instruction executes
  u16 original_pc;
  bool returns; // whether the instruction returns a value

  union {
    // for newarray
    type_kind array_type;
    // constant pool index or local variable index or branch target (instruction
    // index)
    struct {
      u32 index;
      // for branches, the delta to the target in BYTES (so, the pc is scaled by sizeof(bytecode_insn)).
      // For local instructions like astore/dload, equal to max_locals - index
      s32 delta;
    };
    // Integer or long immediate
    s64 integer_imm;
    // Float immediate
    float f_imm;
    // Double immediate
    double d_imm;
    // lookupswitch
    struct lookupswitch_data *lookupswitch;
    // tableswitch
    struct tableswitch_data *tableswitch;
    // iinc
    struct iinc_data iinc;
    // multianewarray
    struct multianewarray_data *multianewarray;
    // non-owned pointer into the constant pool
    cp_entry *cp;
    // anewarray_resolved, checkcast_resolved
    classdesc *classdesc;
  };

  void *ic;
  void *ic2;
} bytecode_insn;

typedef struct {
  u16 max_stack;
  u16 max_locals;
  u32 frame_size;
  int insn_count;
  int max_formal_pc;

  bytecode_insn *code;
  attribute_exception_table *exception_table;
  attribute_line_number_table *line_number_table;
  attribute_local_variable_table *local_variable_table;

  attribute *attributes;
  int attributes_count;
} attribute_code;

typedef struct bootstrap_method {
  cp_method_handle_info *ref;
  cp_entry **args;
  int args_count;
} bootstrap_method;

typedef struct {
  int count;
  bootstrap_method *methods;
} attribute_bootstrap_methods;

typedef struct {
  cp_class_info *class_info;
  cp_name_and_type *nat;
} attribute_enclosing_method;

typedef struct {
  u8 *data;
  int length;
} attribute_runtime_visible_annotations;

typedef struct {
  u8 *data;
  int length;
} attribute_runtime_visible_parameter_annotations;

typedef struct {
  u8 *data;
  int length;
} attribute_runtime_visible_type_annotations;

typedef struct {
  u8 *data;
  int length;
} attribute_annotation_default;

typedef struct {
  slice utf8;
} attribute_signature;

typedef struct {
  slice name;
} attribute_source_file;

typedef struct {
  int count;
  cp_class_info **classes;
} attribute_inner_classes;

// We leave the attribute unparsed until analysis time (which is technically incorrect I think, but whatever)
typedef struct {
  uint8_t *data;
  int32_t length;
  int32_t entries_count;
} attribute_stack_map_table;

typedef struct {
  u16 entries_count;
  cp_class_info **entries;
} attribute_permitted_subclasses;

typedef struct attribute {
  attribute_kind kind;
  slice name;
  u32 length;

  union {
    attribute_code code;
    cp_entry *constant_value;
    attribute_bootstrap_methods bootstrap_methods;
    attribute_enclosing_method enclosing_method;
    attribute_source_file source_file;
    attribute_line_number_table lnt;
    attribute_method_parameters method_parameters;
    attribute_runtime_visible_annotations annotations;
    attribute_runtime_visible_parameter_annotations parameter_annotations;
    attribute_runtime_visible_type_annotations type_annotations;
    attribute_annotation_default annotation_default;
    attribute_signature signature;
    attribute_local_variable_table lvt;
    attribute_stack_map_table smt;
    attribute_inner_classes inner_classes;
    attribute_permitted_subclasses permitted_subclasses;
    cp_class_info *nest_host;
  };
} attribute;

typedef struct code_analysis code_analysis;

typedef struct cp_method {
  access_flags access_flags;

  slice name;
  slice unparsed_descriptor;

  method_descriptor *descriptor;
  code_analysis *code_analysis;

  int attributes_count;
  attribute *attributes;
  attribute_code *code;

  char template_frame[40];  // holds a stack_frame that is a template for an interpreter entry

  // Whether the method may be missing a StackMapTable because it's in an old class file
  bool missing_smt;
  bool is_signature_polymorphic;
  // Whether the method is a constructor (i.e., its name is <init>)
  bool is_ctor;
  // Whether the method is a <clinit> (class initialization method)
  bool is_clinit;
  classdesc *my_class;

  // Index in the vtable, if applicable. Only set at class link time.
  size_t vtable_index;
  // Index in the itable, if applicable.
  size_t itable_index;
  int my_index;        // index in the method table of the class
  void *native_handle; // native_callback

  struct native_Constructor *reflection_ctor;
  struct native_Method *reflection_method;
  struct native_MethodType *method_type_obj;

  // Rough number of times this method has been called. Used for JIT heuristics.
  // Not at all exact because of interrupts.
  int call_count;

  // This method overrides a method in a superclass
  bool overrides;

  void *jit_entry;    // if NULL, there's no way to call this function from JITed code D:
  void *trampoline;   // if NULL, there's no way to call this function from the interpreter D:
  bool jit_available; // whether jit_entry is NOT the interpreter entry but rather a JITed result
  void *jit_info;
} cp_method;

int method_argc(const cp_method *method);

typedef struct cp_field {
  access_flags access_flags;
  slice name;
  slice descriptor;

  int attributes_count;
  attribute *attributes;
  // Offset of the field in the static or instance data area
  size_t byte_offset;

  field_descriptor parsed_descriptor;
  struct native_Field *reflection_field;

  classdesc *my_class;
} cp_field;

typedef struct module module;

typedef struct {
  u32 count;
  u16 slots_unscaled[]; // must be scaled up by 4
} reference_list;

typedef struct classloader classloader;

#define MAX_CF_NAME_LENGTH 1000

// Class descriptor. (Roughly equivalent to HotSpot's InstanceKlass)
typedef struct classdesc {
  classdesc_kind kind;
  bool is_hidden;  // whether this is a hidden class (added in Java 15)
  classdesc_state state; // guarded by lock
  constant_pool *pool; // guarded by lock

  pthread_mutex_t lock; // protects classdesc state

  access_flags access_flags;
  slice name;
  cp_class_info *self;
  cp_class_info *super_class;
  cp_class_info *nest_host;

  int interfaces_count;
  cp_class_info **interfaces;

  int fields_count;
  cp_field *fields;

  int methods_count;
  cp_method *methods;

  attribute_bootstrap_methods *bootstrap_methods;
  attribute_source_file *source_file;
  attribute_inner_classes *inner_classes;

  int attributes_count;
  attribute *attributes;
  classdesc *array_type; // lazily initialized; synchronized access

  u8 *static_fields;
  // Number of bytes (including the object header) which an instance of this
  // class takes up. Unused for array types.
  size_t instance_bytes;

  struct native_Class *mirror; // protected by lock
  struct native_ConstantPool *cp_mirror; // protected by lock

  // Non-array classes: which 4- (32-bit system) or 8-byte aligned offsets correspond to references that need to be
  // followed. Only defined at linkage time.
  reference_list *static_references;
  reference_list *instance_references; // duplicates all superclass fields for convenience/locality

  classdesc *one_fewer_dim; // NULL for non-array types
  classdesc *base_component;

  int dimensions;                // array types only
  type_kind primitive_component; // primitives and array types only

  bytecode_insn **indy_insns;    // used to get GC roots to CallSites
  bytecode_insn **sigpoly_insns; // used to get GC roots to MethodTypes

  module *module;
  classloader *classloader; // class loader which defined this class
  void *linkage_error;

  vtable vtable;
  itables itables;

  classdesc **hierarchy; // 0 = java/lang/Object, etc. Used for fast instanceof checks
  int hierarchy_len;

  arena arena; // most things are allocated in here

  // The tid of the thread which is initializing this class
  s32 initializing_thread; // protected by lock
} classdesc;

heap_string insn_to_string(const bytecode_insn *insn, int insn_index);
attribute *find_attribute_by_kind(classdesc *desc, attribute_kind kind);

char *parse_field_descriptor(const char **chars, size_t len, field_descriptor *result, arena *arena);
char *parse_method_descriptor(slice entry, method_descriptor *result, arena *arena);

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
 * @param error Where to write the error string. If nullptr, error is ignored.
 */
parse_result_t parse_classfile(const u8 *bytes, size_t len, classdesc *result, heap_string *error);

// Implemented in pretty_print.c

const char *cp_kind_to_string(cp_kind kind);
const char *insn_code_to_string(insn_code_kind code);
const char *type_kind_to_string(type_kind kind);
char *cp_class_info_to_string(cp_kind kind, const cp_class_info *ent);
char *cp_name_and_type_to_string(const cp_name_and_type *name_and_type);
char *cp_indy_info_to_string(const cp_indy_info *indy_info);
char *cp_entry_to_string(const cp_entry *ent);
heap_string code_attribute_to_string(const attribute_code *attrib);

#ifdef __cplusplus
}
#endif

#endif // CLASSFILE_H
