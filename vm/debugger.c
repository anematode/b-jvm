//
// Created by Cowpox on 2/13/25.
//

#include "debugger.h"

debugger_bkpt *list_breakpoints(vm *vm, slice filename, int line) {
  debugger_bkpt *lst = nullptr;
  classdesc *C;
  hash_table_iterator it = hash_table_get_iterator(&vm->bootstrap_classloader->loaded);
  char *key;
  size_t len;
  while (hash_table_iterator_has_next(it, &key, &len, (void **)&C)) {
    hash_table_iterator_next(&it);
    if (is_builtin_class((slice){.chars = key, .len = len})) {
      // don't allow breakpoints in built-in classes -> less filename confusion
      continue;
    }
    if (!C->source_file || !utf8_equals_utf8(C->source_file->name, filename)) {
      continue;
    }
    // Search all methods, skipping methods where the line number table is not present or has minimum/maximum that
    // don't encompass the requested line number
    for (int i = 0; i < C->methods_count; ++i) {
      cp_method *method = C->methods + i;
      attribute_line_number_table *lnt;
      if (!method->code || !((lnt = method->code->line_number_table)) || lnt->entry_count == 0)
        continue;
      if (lnt->entries[0].line > line || lnt->entries[lnt->entry_count - 1].line < line)
        continue;
      // Find the first entry whose line number is >= the requested line number. This is our breakpoint.
      for (int j = 0; j < lnt->entry_count; ++j) {
        if (lnt->entries[j].line >= line) {
          debugger_bkpt *bkpt = arraddnptr(lst, 1);
          bkpt->method = method;
          bkpt->pc = lnt->entries[j].start_pc;
          bkpt->replaced_kind = 0; // not used here
          break;
        }
      }

      if (arrlen(lst) > DEBUGGER_MAX_BREAKPOINTS)
        break;
    }
  }
  return lst;
}