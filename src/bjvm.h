//
// Created by Cowpox on 12/10/24.
//

#ifndef BJVM_H
#define BJVM_H

#include <stdint.h>
#include <wchar.h>

#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Instruction code. Similar instructions like aload_0 are canonicalised to
 * aload with an argument of 0.
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
  bjvm_insn_ret
} bjvm_insn_code_kind;

typedef enum {
  BJVM_TYPE_KIND_BOOLEAN = 4,
  BJVM_TYPE_KIND_CHAR = 5,
  BJVM_TYPE_KIND_FLOAT = 6,
  BJVM_TYPE_KIND_DOUBLE = 7,
  BJVM_TYPE_KIND_BYTE = 8,
  BJVM_TYPE_KIND_SHORT = 9,
  BJVM_TYPE_KIND_INT = 10,
  BJVM_TYPE_KIND_LONG = 11,
  BJVM_TYPE_KIND_VOID = 12,

  BJVM_TYPE_KIND_REFERENCE = 13,
  BJVM_TYPE_KIND_RETURN_ADDRESS = 14 // used by jsr/jsr_w
} bjvm_type_kind;

struct bjvm_bc_tableswitch_data {
  int default_target;
  int *targets;
  int targets_count;

  int low, high;
};

struct bjvm_bc_lookupswitch_data {
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

typedef struct bjvm_classdesc bjvm_classdesc;
typedef struct bjvm_obj_header bjvm_obj_header;
typedef struct bjvm_bootstrap_method bjvm_bootstrap_method;
typedef struct bjvm_cp_entry bjvm_cp_entry;

typedef struct {
  bjvm_classdesc *classdesc;
  bjvm_utf8 *name;

  bjvm_obj_header *resolution_error;
} bjvm_cp_class_info;

typedef struct bjvm_cp_name_and_type {
  bjvm_utf8 *name;
  bjvm_utf8 *descriptor;
} bjvm_cp_name_and_type;

typedef struct {
  bjvm_type_kind kind;
  // Can be nonzero for any kind
  int dimensions;
  bjvm_utf8 class_name; // For reference and array types only
} bjvm_field_descriptor;

bool bjvm_is_field_wide(bjvm_field_descriptor desc);

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
  bjvm_cp_name_and_type *name_and_type;
  bjvm_method_descriptor *method_descriptor;
} bjvm_cp_method_info;

typedef struct {
  bjvm_utf8 *chars;
} bjvm_cp_string_info;

typedef struct {
  // Sign extended if original entry was an Integer
  int64_t value;
} bjvm_cp_integral_info;

typedef struct {
  // Extended if original entry was a Float
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
} bjvm_cp_method_handle_info;

typedef struct {
  bjvm_utf8 *descriptor;
} bjvm_cp_method_type_info;

typedef struct {
  bjvm_bootstrap_method *method; // TODO convert to pointer
  bjvm_cp_name_and_type *name_and_type;
  bjvm_method_descriptor *method_descriptor;
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
    bjvm_utf8 utf8;
    bjvm_cp_string_info string;

    bjvm_cp_floating_info floating;
    bjvm_cp_integral_info integral;

    bjvm_cp_name_and_type name_and_type;
    bjvm_cp_class_info class_info;

    bjvm_cp_field_info fieldref_info;
    bjvm_cp_method_info methodref;
    bjvm_cp_method_handle_info method_handle_info;
    bjvm_cp_method_type_info method_type_info;
    bjvm_cp_indy_info indy_info;
  };
} bjvm_cp_entry;

struct bjvm_multianewarray_data {
  bjvm_cp_class_info *entry;
  uint8_t dimensions;
};

typedef struct {
  bjvm_insn_code_kind kind;
  int program_counter;

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
  // Per-instruction resolved data
  void *ic;
  void *ic2;
  int args;
} bjvm_bytecode_insn;

typedef struct bjvm_constant_pool {
  int entries_len;
  bjvm_cp_entry entries[];
} bjvm_constant_pool;

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
  BJVM_ATTRIBUTE_KIND_CODE = 0,
  BJVM_ATTRIBUTE_KIND_CONSTANT_VALUE = 1,
  BJVM_ATTRIBUTE_KIND_UNKNOWN = 2,
  BJVM_ATTRIBUTE_KIND_BOOTSTRAP_METHODS = 3,
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
  int handler_pc;
  bjvm_cp_class_info *catch_type;
} bjvm_exception_table_entry;

typedef struct {
  bjvm_exception_table_entry *entries;
  int entries_count;
} bjvm_attribute_exception_table;

typedef struct bjvm_attribute bjvm_attribute;

typedef struct {
  uint16_t max_stack;
  uint16_t max_locals;
  int insn_count;
  int max_formal_pc;

  bjvm_bytecode_insn *code;
  bjvm_attribute_exception_table *exception_table;

  bjvm_attribute *attributes;
  int attributes_count;
} bjvm_attribute_code;

typedef struct bjvm_bootstrap_method {
  bjvm_cp_method_handle_info* ref;
  bjvm_cp_entry **args;
  int args_count;
} bjvm_bootstrap_method;

typedef struct {
  int count;
  bjvm_bootstrap_method * methods;
} bjvm_attribute_bootstrap_methods;

typedef struct bjvm_attribute {
  bjvm_attribute_kind kind;
  bjvm_utf8 *name;
  uint32_t length;

  union {
    bjvm_attribute_code code;
    bjvm_cp_entry *constant_value;
    bjvm_attribute_bootstrap_methods bootstrap_methods;
  };
} bjvm_attribute;

typedef struct {
  union {
    // Used if the number of bits in the local variable table/stack is less than
    // 64
    uint64_t bits_inl;
    struct {
      uint64_t *bits;
      uint32_t size_words;
    } ptr;
  };
} bjvm_compressed_bitset;

// Result of the analysis of a code segment. During analysis, stack operations
// on longs/doubles are simplified as if they only took up one stack slot (e.g.,
// pop2 on a double becomes a pop, while pop2 on two ints stays as a pop2).
// Also, we progressively resolve the state of the stack and local variable
// table at each program counter, and store a bitset of which stack/local
// variables are references, so that the GC can follow them.
typedef struct {
  bjvm_compressed_bitset *insn_index_to_references;
  int insn_count;
} bjvm_code_analysis;

typedef struct bjvm_thread bjvm_thread;

/**
 * For simplicity, we always store local variables/stack variables as 64 bits,
 * and only use part of them in the case of integer or float (or, in 32-bit
 * mode, reference) values.
 */
typedef union {
  int64_t l;
  int i; // used for all integer types except long, plus boolean
  float f;
  double d;
  bjvm_obj_header *obj; // reference type
} bjvm_stack_value;

typedef bjvm_stack_value (*bjvm_native_callback)(bjvm_thread *vm, bjvm_obj_header *obj,
                                    bjvm_stack_value *args, int argc);

typedef struct bjvm_cp_method {
  bjvm_access_flags access_flags;

  bjvm_utf8 *name;
  bjvm_utf8 *descriptor;

  bjvm_method_descriptor *parsed_descriptor;
  bjvm_code_analysis *code_analysis;

  int attributes_count;
  bjvm_attribute *attributes;
  bjvm_attribute_code *code;

  bool is_signature_polymorphic;
  bjvm_classdesc *my_class;

  bjvm_native_callback native_handle;
  struct bjvm_native_Constructor *reflection_ctor;
} bjvm_cp_method;

typedef struct bjvm_cp_field {
  bjvm_access_flags access_flags;
  bjvm_utf8 *name;
  bjvm_utf8 *descriptor;

  int attributes_count;
  bjvm_attribute *attributes;
  // Offset of the field in the static or instance data area
  int byte_offset;

  bjvm_field_descriptor parsed_descriptor;
  struct bjvm_native_Field *reflection_field;

  bjvm_classdesc *my_class;
} bjvm_cp_field;

typedef enum {
  BJVM_CD_KIND_ORDINARY_ARRAY = 0,
  BJVM_CD_KIND_BYTE_ARRAY = 1,
  BJVM_CD_KIND_CHAR_ARRAY = 2,
  BJVM_CD_KIND_DOUBLE_ARRAY = 3,
  BJVM_CD_KIND_FLOAT_ARRAY = 4,
  BJVM_CD_KIND_INT_ARRAY = 5,
  BJVM_CD_KIND_LONG_ARRAY = 6,
  BJVM_CD_KIND_SHORT_ARRAY = 7,
  BJVM_CD_KIND_BOOLEAN_ARRAY = 8,
  BJVM_CD_KIND_ORDINARY = 10,
} bjvm_classdesc_kind;

typedef struct bjvm_array_classdesc bjvm_array_classdesc;

typedef enum {
  BJVM_CD_STATE_LINKAGE_ERROR = 0,
  BJVM_CD_STATE_LOADED = 1,
  BJVM_CD_STATE_LINKED = 2,
  BJVM_CD_STATE_INITIALIZED = 3,
} bjvm_classdesc_state;

typedef struct bjvm_classdesc {
  bjvm_classdesc_kind kind;
  bjvm_classdesc_state state;

  bjvm_constant_pool *pool;

  bjvm_access_flags access_flags;
  bjvm_utf8 name;
  bjvm_cp_class_info *super_class;

  int interfaces_count;
  bjvm_cp_class_info **interfaces;

  int fields_count;
  bjvm_cp_field *fields;

  int methods_count;
  bjvm_cp_method *methods;

  int bootstrap_methods_count;
  bjvm_attribute_bootstrap_methods *bootstrap_methods;

  int attributes_count;
  bjvm_attribute *attributes;

  bjvm_classdesc *array_type;

  // Whether this class corresponds to the primordial object class
  bool is_primordial_object;
  uint16_t minor_version;
  uint16_t major_version;

  uint8_t *static_fields;
  int data_bytes;
  // Padding bytes before nonstatic fields for implementation details (e.g.
  // pointers to VM constructs)
  int imp_padding;

  struct bjvm_native_Class *mirror;

  // Non-array classes: which 4- or 8-byte aligned offsets correspond to references that need to be followed
  bjvm_compressed_bitset static_references;
  bjvm_compressed_bitset instance_references;

  bjvm_classdesc *one_fewer_dim;
  bjvm_classdesc *base_component;
} bjvm_classdesc;

typedef uint64_t bjvm_mark_word_t;

typedef struct bjvm_ordinary_class bjvm_ordinary_class;

typedef struct bjvm_array_classdesc {
  bjvm_classdesc base;
  int dimensions;
} bjvm_array_classdesc;

typedef struct bjvm_primitive_array_classdesc {
  bjvm_classdesc base;
  int dimensions;
  bjvm_type_kind element_type;
} bjvm_primitive_array_classdesc;

// Equivalent to HotSpot's InstanceKlass
typedef struct bjvm_ordinary_class {
  bjvm_classdesc base;
  bjvm_classdesc classfile;
} bjvm_ordinary_classdesc;

// Appears at the top of every object -- corresponds to HotSpot's oopDesc
typedef struct bjvm_obj_header {
  volatile bjvm_mark_word_t mark_word;
  bjvm_classdesc *descriptor;
} bjvm_obj_header;

struct bjvm_class_loader;
typedef struct bjvm_vm bjvm_vm;

typedef struct bjvm_hash_table_entry {
  struct bjvm_hash_table_entry *next;
  wchar_t *key;
  uint32_t key_len;
  void *data;
} bjvm_hash_table_entry;

typedef struct bjvm_string_hash_table_iterator {
  bjvm_hash_table_entry *current_base;
  bjvm_hash_table_entry *current;
  bjvm_hash_table_entry *end;
} bjvm_hash_table_iterator;

// Separate chaining hash map from (wchar_t) strings to void* (arbitrary)
// entries. Sigh.
typedef struct bjvm_string_hash_table {
  void (*free_fn)(void *entry);
  bjvm_hash_table_entry *entries;

  size_t entries_count;
  size_t entries_cap;

  double load_factor;
} bjvm_string_hash_table;

bjvm_string_hash_table bjvm_make_hash_table(void (*free_fn)(void *),
                                            double load_factor,
                                            size_t initial_capacity);

bjvm_hash_table_iterator
bjvm_hash_table_get_iterator(bjvm_string_hash_table *tbl);

bool bjvm_hash_table_iterator_has_next(bjvm_hash_table_iterator iter,
                                       wchar_t **key, size_t *key_len,
                                       void **value);

bool bjvm_hash_table_iterator_next(bjvm_hash_table_iterator *iter);

uint32_t bjvm_hash_string(const wchar_t *key, size_t len);

void bjvm_hash_table_rehash(bjvm_string_hash_table *tbl, size_t new_capacity);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. Ownership of the key is passed into the function.
 */
void *bjvm_hash_table_insert_impl(bjvm_string_hash_table *tbl, wchar_t *key,
                                  int len, void *value, bool copy_key);

/**
 * Insert the key/value pair into the hash table and return the old value, if
 * any. If len = -1, the key is treated as a null-terminated string literal.
 */
[[nodiscard]] void *bjvm_hash_table_insert(bjvm_string_hash_table *tbl,
                                           const wchar_t *key, int len,
                                           void *value);

/**
 * Delete the key from the hash table and return the old value, if any. If len =
 * -1, the key is treated as a null- terminated string literal. Pass the result
 * of this function to the free function, as long as it accepts NULL pointers.
 */
[[nodiscard]] void *bjvm_hash_table_delete(bjvm_string_hash_table *tbl,
                                           const wchar_t *key, int len);

/**
 * Look up the value in the hash table.
 */
void *bjvm_hash_table_lookup(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len);

void bjvm_free_hash_table(bjvm_string_hash_table tbl);

typedef void (*bjvm_write_byte)(int ch, void *param);

typedef struct bjvm_vm {
  // Map class file name -> cf bytes
  bjvm_string_hash_table classfiles;
  // Map class name (e.g. "java/lang/String") to classdesc*
  bjvm_string_hash_table classes;
  // Classes currently under creation -- used to detect circularity
  bjvm_string_hash_table inchoate_classes;

  // Native methods in javah form
  bjvm_string_hash_table natives;

  int (*load_classfile)(const char *filename, void *param, uint8_t **bytes,
                        size_t *len);
  void *load_classfile_param;

  // Main thread group
  bjvm_obj_header *main_thread_group;

  // Interned strings (string -> instance of java/lang/String)
  bjvm_string_hash_table interned_strings;

  // Classes with implementation-required padding before other fields (map class name -> padding bytes)
  bjvm_string_hash_table class_padding;

  // Write byte of stdout/stderr (if NULL, uses the default implementation)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;

  // Passed to write_stdout/write_stderr
  void *write_byte_param;

  // Primitive classes (int.class, etc., boolean (4 -> 0) through void (12 -> 8) )
  struct bjvm_native_Class *primitive_classes[9];
} bjvm_vm;

typedef struct {
  bool unused;

  // Callback to load a classfile from the classpath. Returns 0 on success,
  // nonzero on failure. Pointer passed to bytes will be free()-d by the VM.
  int (*load_classfile)(const char *filename, void *param, uint8_t **bytes,
                        size_t *len);
  void *load_classfile_param;

  // Write byte of stdout/stderr (if NULL, uses the default implementation)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;
  // Passed to write_stdout/write_stderr
  void *write_byte_param;
} bjvm_vm_options;

typedef struct {
  int program_counter; // in instruction indices
  int max_stack;
  int max_locals;
  int stack_depth;

  bjvm_cp_method *method;

  // First max_locals bjvm_stack_values, then max_stack more
  bjvm_stack_value values[];
} bjvm_stack_frame;

typedef struct bjvm_thread {
  // Global VM corresponding to this thread
  bjvm_vm *vm;

  // Essentially the stack of the thread -- a contiguous buffer which stores
  // stack_frames
  uint8_t *frame_buffer;
  uint32_t frame_buffer_capacity;
  // Also pointer one past the end of the last stack frame
  uint32_t frame_buffer_used;

  // Pointers into the frame_buffer
  bjvm_stack_frame **frames;
  uint32_t frames_count;
  uint32_t frames_cap;

  // Currently propagating exception
  bjvm_obj_header *current_exception;

  bool js_jit_enabled;

  // Instance of java.lang.Thread
  struct bjvm_native_Thread *thread_obj;
} bjvm_thread;

bjvm_array_classdesc *
bjvm_checked_to_array_classdesc(bjvm_classdesc *classdesc);
bjvm_classdesc *
bjvm_checked_to_primitive_array_classdesc(bjvm_classdesc *classdesc);

/**
 * Create an uninitialized frame with space sufficient for the given method.
 * Raises a StackOverflowError if the frames are exhausted.
 */
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method);

/**
 * Pop the topmost frame from the stack, optionally passing a pointer as a debug
 * check.
 */
void bjvm_pop_frame(bjvm_thread *thr, const bjvm_stack_frame *reference);

bjvm_vm *bjvm_create_vm(bjvm_vm_options options);

typedef struct {
  uint32_t stack_space;
  // Whether to enable JavaScript JIT compilation
  bool js_jit_enabled;
  // What thread group to construct the thread in (NULL = default thread group)
  bjvm_obj_header *thread_group;
  // Write byte of stdout/stderr (if NULL, prints directly to stdout/stderr)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;
} bjvm_thread_options;

bjvm_thread_options bjvm_default_thread_options();
bjvm_thread *bjvm_create_thread(bjvm_vm *vm, bjvm_thread_options options);
void bjvm_free_thread(bjvm_thread *thread);

/**
 * Directly add the given classfile as accessible to the VM, bypassing the
 * callback to load_classfile.
 */
int bjvm_vm_preregister_classfile(bjvm_vm *vm, const wchar_t *filename,
                                  const uint8_t *bytes, size_t len);

/**
 * Read the classfile in the class path. Returns -1 on failure to find the
 * classfile. Writes a pointer to the classfile bytes and the length of the
 * classfile to the given pointers.
 */
int bjvm_vm_read_classfile(bjvm_vm *vm, const wchar_t *filename,
                           const uint8_t **bytes, size_t *len);

void bjvm_vm_list_classfiles(bjvm_vm *vm, wchar_t **strings, size_t *count);

/**
 * Parse a Java class file.
 *
 * The error message corresponds to a ClassFormatError in Java land.
 * (UnsupportedClassVersionErrors and VerifyErrors should be raised elsewhere.)
 *
 * @param bytes Start byte of the classfile.
 * @param len Length of the classfile in bytes.
 * @param result Where to write the result.
 * @return NULL on success, otherwise an error message (which is the caller's
 * responsibility to free).
 */
char *bjvm_parse_classfile(uint8_t *bytes, size_t len, bjvm_classdesc *result);

/**
 * Free the classfile.
 */
void bjvm_free_classfile(bjvm_classdesc cf);

void bjvm_free_vm(bjvm_vm *vm);

/**
 * Implementation details, but exposed for testing...
 */

typedef bjvm_type_kind bjvm_analy_stack_entry;

// State of the stack (or local variable table) during analysis, indexed by
// formal JVM semantics (i.e., long/double take up two slots, and the second
// slot is unusable).
typedef struct {
  bjvm_analy_stack_entry *entries;
  int entries_count;
  int entries_cap;
  bool from_jump_target;
} bjvm_analy_stack_state;

char *print_analy_stack_state(const bjvm_analy_stack_state *state);

bjvm_compressed_bitset bjvm_init_compressed_bitset(int bits_capacity);
void bjvm_free_compressed_bitset(bjvm_compressed_bitset bits);
bool bjvm_is_bitset_compressed(bjvm_compressed_bitset bits);
bjvm_compressed_bitset bjvm_empty_bitset();
int *bjvm_list_compressed_bitset_bits(bjvm_compressed_bitset bits,
                                      int *existing_buf, int *length,
                                      int *capacity);
bool bjvm_test_compressed_bitset(bjvm_compressed_bitset bits, size_t bit_index);
bool bjvm_test_reset_compressed_bitset(bjvm_compressed_bitset *bits,
                                       size_t bit_index);
bool bjvm_test_set_compressed_bitset(bjvm_compressed_bitset *bits,
                                     size_t bit_index);

char *bjvm_locals_on_function_entry(const bjvm_utf8 *descriptor,
                                    bjvm_analy_stack_state *locals);
char *parse_field_descriptor(const wchar_t **chars, size_t len,
                             bjvm_field_descriptor *result);
char *parse_method_descriptor(const bjvm_utf8 *descriptor,
                              bjvm_method_descriptor *result);
bool utf8_equals(const bjvm_utf8 *entry, const char *str);
char *lossy_utf8_entry_to_chars(const bjvm_utf8 *utf8);
bjvm_utf8 bjvm_make_utf8(const wchar_t *c_literal);
void free_utf8(bjvm_utf8 entry);
void free_field_descriptor(bjvm_field_descriptor descriptor);
bjvm_classdesc *bootstrap_class_create(bjvm_thread *thread,
                                       const wchar_t *name);
int bjvm_link_class(bjvm_thread *thread, bjvm_classdesc *classdesc);
bjvm_cp_method *bjvm_easy_method_lookup(bjvm_classdesc *classdesc,
                                        const char *name,
                                        const char *descriptor,
                                        bool superclasses,
                                        bool superinterfaces);
bjvm_utf8 bjvm_make_utf8_cstr(const char *c_literal);
int bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method,
                     bjvm_stack_value *args, bjvm_stack_value *result);
int bjvm_initialize_class(bjvm_thread *thread, bjvm_classdesc *classdesc);
void bjvm_register_native(bjvm_vm *vm, const char *class_name,
                          const char *method_name,
                          const char *method_descriptor,
                          bjvm_native_callback callback);

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc);

int *array_length(bjvm_obj_header *array);

void *array_data(bjvm_obj_header *array);

bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror);

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror);

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror);

bjvm_obj_header *create_object_array(bjvm_thread *thread,
                                     bjvm_classdesc *classdesc, int count);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field,
                    bjvm_stack_value bjvm_stack_value);
bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field);
bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc,
                                      const wchar_t *name,
                                      const wchar_t *descriptor);
bjvm_type_kind field_to_representable_kind(const bjvm_field_descriptor *field);
int bjvm_raise_exception(bjvm_thread *thread, const wchar_t *exception_name, const wchar_t *exception_string);

// e.g. int.class
struct bjvm_native_Class *bjvm_primitive_class_mirror(bjvm_thread *thread, bjvm_type_kind prim_kind);

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread, const wchar_t *chars,
                                    size_t len);
#include "natives.h"

#ifdef __cplusplus
}
#endif

#endif // BJVM_H
