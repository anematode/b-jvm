#include <assert.h>

#include "../src/bjvm.h"
#include "../src/linkage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct class_def {
  char *name;
  int imp_padding;
  char *fields;
};

void print_field(const bjvm_cp_field *field) {
  if ((field->access_flags & BJVM_ACCESS_STATIC))
    return;
  const char *name;
  switch (field_to_kind(&field->parsed_descriptor)) {
  case BJVM_TYPE_KIND_FLOAT:
    name = "float ";
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    name = "double ";
    break;
  case BJVM_TYPE_KIND_INT:
    name = "s32 ";
    break;
  case BJVM_TYPE_KIND_LONG:
    name = "s64 ";
    break;
  case BJVM_TYPE_KIND_REFERENCE:
    name = "bjvm_obj_header *";
    break;
  case BJVM_TYPE_KIND_BOOLEAN:
    name = "_Alignas(4) bool ";
    break;
  case BJVM_TYPE_KIND_CHAR:
    name = "_Alignas(4) u16 ";
    break;
  case BJVM_TYPE_KIND_BYTE:
    name = "_Alignas(4) s8 ";
    break;
  case BJVM_TYPE_KIND_SHORT:
    name = "_Alignas(4) s16 ";
    break;
  default:
  case BJVM_TYPE_KIND_VOID:
    UNREACHABLE();
  }
  printf("  %s%.*s;  // %.*s\n", name, fmt_slice(field->name),
         fmt_slice(field->descriptor));
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <natives.txt> <jre directory>\n", argv[0]);
    return 1;
  }

  bjvm_vm_options options = bjvm_default_vm_options();

  bjvm_vm *vm = bjvm_create_vm(options);
  bjvm_thread *thread = bjvm_create_thread(vm, bjvm_default_thread_options());

  // Read natives.txt line by line
  FILE *f = fopen(argv[1], "r");
  if (!f) {
    fprintf(stderr, "Failed to open %s\n", argv[1]);
    return 1;
  }

  char *lines[100];
  size_t line_count = 0;

  struct class_def classes[100];
  int class_count = 0;

  while (!feof(f)) {
    char *line = nullptr;
    size_t len = 0;
    ssize_t read = getline(&line, &len, f);
    if (read == -1) {
      break;
    }
    assert(line_count < 100);
    lines[line_count++] = line;
  }

  // Packing rules: all ints/boolean become int, float/double/long stays,
  // reference types are pointers

  for (size_t i = 0; i < line_count; ++i) {
    char *line = lines[i];
    if (!isspace(line[0])) { // class name
      int padding = 0;
      char fields[1024] = {0};
      size_t j = i + 1;
      for (; j < line_count; ++j) {
        char *line = lines[j];
        if (isspace(line[0])) {
          padding += 1;
          strcat(fields, line);
          strcat(fields, "\n");
        } else {
          break;
        }
      }
      line[strlen(line) - 1] = '\0';
      classes[class_count++] =
          (struct class_def){line, padding, strdup(fields)};
      i = j - 1;
    }
  }

  printf("/** BEGIN CODEGEN SECTION (gen_natives.c) */\n");

  for (int i = 0; i < class_count; ++i) {
    bjvm_classdesc *desc = bootstrap_lookup_class(thread, (slice) { .chars = classes[i].name, .len = strlen(classes[i].name) });
    assert(desc);
    bjvm_link_class(thread, desc);

    char *last_slash = strrchr(classes[i].name, L'/');
    char *simple_name = last_slash ? last_slash + 1 : classes[i].name;

    // Go through fields and generate the struct definition
    //
    // struct bjvm_native_<suffix> {
    //    bjvm_obj_header base;
    //    <superclass's fields>
    //    <impdep fields>
    //    <my fields>
    // }

    printf("struct bjvm_native_%s {\n", simple_name);
    printf("  bjvm_obj_header base;\n");
    if (desc->super_class) {
      bjvm_classdesc *v = desc->super_class->classdesc;
      bjvm_classdesc *supers[10];
      int supers_count = 0;
      while (v && v->super_class) {
        supers[supers_count++] = v;
        v = v->super_class->classdesc;
      }
      bool comment = false;
      for (int j = supers_count - 1; j >= 0; --j) {
        for (int i = 0; i < supers[j]->fields_count; ++i) {
          if (!comment) {
            comment = true;
            printf("  // superclass fields\n");
          }
          bjvm_cp_field *field = &supers[j]->fields[i];
          print_field(field);
        }
      }
    }

    // Add implementation-dependent fields
    if (strlen(classes[i].fields) > 1) {
      printf("  // implementation-dependent fields\n");
      printf("%s", classes[i].fields);
    }

    printf("  // my fields\n");
    // Add my fields
    for (int j = 0; j < desc->fields_count; ++j) {
      bjvm_cp_field *field = &desc->fields[j];
      print_field(field);
    }

    printf("};\n");
  }

  printf("static inline void bjvm_register_native_padding(bjvm_vm *vm) {\n");

  for (int i = 0; i < class_count; ++i) {
    if (classes[i].imp_padding)
      printf("  (void)bjvm_hash_table_insert(&vm->class_padding, \"%s\", -1, "
             "(void*) (%d * sizeof(void*)));\n",
             classes[i].name, classes[i].imp_padding);
  }

  printf("}\n");
  printf("/** END CODEGEN SECTION (gen_natives.c) */\n");
}