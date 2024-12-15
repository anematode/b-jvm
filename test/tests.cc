#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../src/bjvm.h"

bool EndsWith(const std::string &s, const std::string &suffix) {
  if (s.size() < suffix.size()) {
    return false;
  }
  return s.substr(s.size() - suffix.size()) == suffix;
}

bool HasSuffix(std::string_view str, std::string_view suffix) {
  // Credit: https://stackoverflow.com/a/20446239/13458117
  return str.size() >= suffix.size() &&
         str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

double get_time() {
#ifdef EMSCRIPTEN
  return emscripten_get_now();
#else
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
#endif
}

std::vector<uint8_t> ReadFile(const std::string &file) {
#ifdef EMSCRIPTEN
  void *length_and_data = EM_ASM_PTR(
      {
        const fs = require('fs');
        const buffer = fs.readFileSync(UTF8ToString($0));
        const length = buffer.length;

        const result = _malloc(length + 4);
        Module.HEAPU32[result >> 2] = length;
        Module.HEAPU8.set(buffer, result + 4);

        return result;
      },
      file.c_str());

  uint32_t length = *reinterpret_cast<uint32_t *>(length_and_data);
  uint8_t *data = reinterpret_cast<uint8_t *>(length_and_data) + 4;

  std::vector result(data, data + length);
  free(length_and_data);
  return result;
#else // !EMSCRIPTEN
  std::vector<uint8_t> result;
  if (std::filesystem::exists(file)) {
    std::ifstream ifs(file, std::ios::binary | std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();

    result.resize(pos);
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char *>(result.data()), pos);
  } else {
    throw std::runtime_error("Classpath file not found: " + file);
  }
  return result;
#endif
}

std::vector<std::string> ListDirectory(const std::string &path,
                                       bool recursive) {
#ifdef EMSCRIPTEN
  void *length_and_data = EM_ASM_PTR(
      {
        // Credit: https://stackoverflow.com/a/5827895
        const fs = require('fs');
        const path = require('path');
        function *walkSync(dir, recursive) {
          const files = fs.readdirSync(dir, {withFileTypes : true});
          for (const file of files) {
            if (file.isDirectory() && recursive) {
              yield *walkSync(path.join(dir, file.name), recursive);
            } else {
              yield path.join(dir, file.name);
            }
          }
        }

        var s = "";
        for (const filePath of walkSync(UTF8ToString($0), $1))
          s += filePath + "\n";

        const length = s.length;
        const result = _malloc(length + 4);
        Module.HEAPU32[result >> 2] = length;
        Module.HEAPU8.set(new TextEncoder().encode(s), result + 4);

        return result;
      },
      path.c_str(), recursive);

  uint32_t length = *reinterpret_cast<uint32_t *>(length_and_data);
  uint8_t *data = reinterpret_cast<uint8_t *>(length_and_data) + 4;

  std::string s(data, data + length);
  free(length_and_data);

  std::vector<std::string> result;
  size_t start = 0;

  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\n') {
      result.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }

  return result;
#else // !EMSCRIPTEN
  using namespace std::filesystem;

  // Recursively list files (TODO: fix recursive = false)
  std::vector<std::string> result;

  (void)recursive;

  for (const auto &entry : recursive_directory_iterator(path)) {
    if (entry.is_regular_file()) {
      result.push_back(entry.path().string());
    }
  }

  return result;
#endif
}
TEST_CASE("Test classfile parsing") {
  bool fuzz = false;

  // list all java files in the jre8 directory
  auto files = ListDirectory("jre8", true);
  double total_millis = 0;

  char *shortest_error = NULL;
  int shortest_error_length = 1000000;

  int count = 0;
  int FILE_COUNT = fuzz ? 64 : INT_MAX;
  for (const auto &file : files) {
    if (!EndsWith(file, ".class")) {
      continue;
    }
    if (count++ > FILE_COUNT) {
      continue;
    }

    auto read = ReadFile(file);
    double start = get_time();

    // std::cout << "Reading " << file << "\n";
    bjvm_classdesc cf;
    char *error = bjvm_parse_classfile(read.data(), read.size(), &cf);
    if (error != nullptr) {
      std::cerr << "Error parsing classfile: " << error << '\n';
      if ((int)strlen(error) < shortest_error_length) {
        shortest_error = strdup(error);
        shortest_error_length = strlen(error);
      }
      free(error);
      // abort();
    } else {
      bjvm_free_classfile(cf);
    }

    if (fuzz) {
      std::cerr << "Fuzzing classfile: " << file << '\n';

#pragma omp parallel for
      for (size_t i = 0; i < read.size(); ++i) {
        std::cout << "Done with " << i << '\n';
        bjvm_classdesc cf;
        auto copy = read;
        for (int j = 0; j < 256; ++j) {
          copy[i] += 1;
          char *error = bjvm_parse_classfile(copy.data(), copy.size(), &cf);
          if (error) {
            free(error);
          } else {
            bjvm_free_classfile(cf);
          }
        }
      }
    }

    total_millis += get_time() - start;
  }

  printf("Shortest error: %s\n", shortest_error);

  std::cout << "Total time: " << total_millis << "ms\n";

  BENCHMARK_ADVANCED("Parse classfile")(Catch::Benchmark::Chronometer meter) {
    auto read = ReadFile("./jre8/sun/security/tools/keytool/Main.class");
    bjvm_classdesc cf;

    meter.measure([&] {
      char *error = bjvm_parse_classfile(read.data(), read.size(), &cf);
      if (error)
        abort();
      bjvm_free_classfile(cf);
    });
  };
}

int preregister_all_classes(bjvm_vm *vm) {
  auto files = ListDirectory("jre8", true);
  int file_count = 0;
  for (auto file : files) {
    if (!EndsWith(file, ".class")) {
      continue;
    }
    auto read = ReadFile(file);
    wchar_t filename[1000] = {};
    file = file.substr(5); // remove "jre8/"
    mbstowcs(filename, file.c_str(), file.size());
    bjvm_vm_preregister_classfile(vm, filename, read.data(), read.size());
    file_count++;
  }
  return file_count;
}

TEST_CASE("Class file management") {
  bjvm_vm_options options = {};
  bjvm_vm *vm = bjvm_create_vm(options);

  int file_count = preregister_all_classes(vm);
  size_t len;
  const uint8_t *bytes;
  REQUIRE(bjvm_vm_read_classfile(vm, L"java/lang/Object.class", &bytes, &len) ==
          0);
  REQUIRE(len > 0);
  REQUIRE(*(uint32_t *)bytes == 0xBEBAFECA);
  REQUIRE(bjvm_vm_read_classfile(vm, L"java/lang/Object.clas", nullptr, &len) !=
          0);
  REQUIRE(bjvm_vm_read_classfile(vm, L"java/lang/Object.classe", nullptr,
                                 &len) != 0);
  bjvm_vm_list_classfiles(vm, nullptr, &len);
  REQUIRE((int)len == file_count);
  std::vector<wchar_t *> strings(len);
  bjvm_vm_list_classfiles(vm, strings.data(), &len);
  bool found = false;
  for (size_t i = 0; i < len; ++i) {
    found = found || wcscmp(strings[i], L"java/lang/ClassLoader.class") == 0;
    free(strings[i]);
  }
  REQUIRE(found);
  bjvm_free_vm(vm);
}

TEST_CASE("Compressed bitset") {
  for (int size = 1; size < 256; ++size) {
    std::vector<uint8_t> reference(size);
    auto bitset = bjvm_init_compressed_bitset(size);

    for (int i = 0; i < 1000; ++i) {
      int index = rand() % size;
      switch (rand() % 4) {
      case 0: {
        int *set_bits = nullptr, length = 0, capacity = 0;
        set_bits = bjvm_list_compressed_bitset_bits(bitset, set_bits, &length,
                                                    &capacity);
        for (int i = 0; i < length; ++i) {
          if (!reference[set_bits[i]])
            REQUIRE(false);
        }
        free(set_bits);
        break;
      }
      case 1: {
        bool test = bjvm_test_set_compressed_bitset(&bitset, index);
        if (test != reference[index])
          REQUIRE(test == reference[index]);
        reference[index] = true;
        break;
      }
      case 2: {
        bool test = bjvm_test_reset_compressed_bitset(&bitset, index);
        if (test != reference[index])
          REQUIRE(test == reference[index]);
        reference[index] = false;
        break;
      }
      case 3: {
        bool test = bjvm_test_compressed_bitset(bitset, index);
        if (test != reference[index])
          REQUIRE(test == reference[index]);
        break;
      }
      }
    }

    bjvm_free_compressed_bitset(bitset);
  }
}

TEST_CASE("parse_field_descriptor valid cases") {
  const wchar_t *fields =
      L"Lcom/example/Example;[I[[[JLjava/lang/String;[[Ljava/lang/Object;BVCZ";
  bjvm_field_descriptor com_example_Example, Iaaa, Jaa, java_lang_String,
      java_lang_Object, B, V, C, Z;
  REQUIRE(
      !parse_field_descriptor(&fields, wcslen(fields), &com_example_Example));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &Iaaa));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &Jaa));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &java_lang_String));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &java_lang_Object));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &B));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &V));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &C));
  REQUIRE(!parse_field_descriptor(&fields, wcslen(fields), &Z));

  REQUIRE(utf8_equals(&com_example_Example.class_name, "com/example/Example"));
  REQUIRE(com_example_Example.dimensions == 0);
  REQUIRE(com_example_Example.kind == BJVM_TYPE_KIND_REFERENCE);

  REQUIRE(Iaaa.kind == BJVM_TYPE_KIND_INT);
  REQUIRE(Iaaa.dimensions == 1);

  REQUIRE(Jaa.kind == BJVM_TYPE_KIND_LONG);
  REQUIRE(Jaa.dimensions == 3);

  REQUIRE(utf8_equals(&java_lang_String.class_name, "java/lang/String"));
  REQUIRE(java_lang_String.dimensions == 0);

  REQUIRE(utf8_equals(&java_lang_Object.class_name, "java/lang/Object"));
  REQUIRE(java_lang_Object.dimensions == 2);

  REQUIRE(B.kind == BJVM_TYPE_KIND_BYTE);
  REQUIRE(C.kind == BJVM_TYPE_KIND_CHAR);
  REQUIRE(V.kind == BJVM_TYPE_KIND_VOID);
  REQUIRE(Z.kind == BJVM_TYPE_KIND_BOOLEAN);

  free_field_descriptor(com_example_Example);
  free_field_descriptor(java_lang_Object);
  free_field_descriptor(java_lang_String);
}

int load_classfile(const char* filename, void* param, uint8_t** bytes, size_t* len) {
  const char** classpath = (const char**) param;
  std::vector<std::string> paths { "jre8/" };
  for (int i = 0; classpath && classpath[i]; ++i) {
    paths.emplace_back(classpath[i]);
  }
  for (const auto& path : paths) {
    std::string file = path + std::string(filename);
    try {
      auto file_data = ReadFile(file);
      if (file_data.size() > 0) {
        auto* data = (uint8_t*)malloc(file_data.size());
        memcpy(data, file_data.data(), file_data.size());
        *bytes = data;
        *len = file_data.size();
        return 0;
      }
    } catch (const std::runtime_error& e) {
    }
  }

  return -1;
}

bjvm_vm *create_vm(bool preregister, const char** classpath = nullptr) {
  bjvm_vm_options options = {};

  options.load_classfile = load_classfile;
  options.load_classfile_param = classpath;
  bjvm_vm *vm = bjvm_create_vm(options);

  if (preregister)
    preregister_all_classes(vm);

  return vm;
}

TEST_CASE("VM initialization") {
  bjvm_vm *vm = create_vm(true);
  bjvm_free_vm(vm);
}

TEST_CASE("Playground") {
  const char* cp[2] = { "test_files/playground/", nullptr };
  bjvm_vm *vm = create_vm(false, cp);

  bjvm_thread_options options;
  bjvm_fill_default_thread_options(&options);
  bjvm_thread *thr = bjvm_create_thread(vm, options);

  bjvm_utf8 java_lang_Object = bjvm_make_utf8(L"Main");
  bjvm_classdesc* desc = bootstrap_class_create(thr, java_lang_Object);
  int status = bootstrap_class_link(thr, desc);
  REQUIRE(status == 0);

  bjvm_cp_method *method = bjvm_get_method(desc, "main", "([Ljava/lang/String;)V");
  bjvm_stack_value args[1] = { 0 };

  bjvm_thread_start(thr, method, args);

  free_utf8(java_lang_Object);

  bjvm_free_thread(thr);
  bjvm_free_vm(vm);
}

TEST_CASE("String hash table") {
  bjvm_string_hash_table tbl = bjvm_make_hash_table(free, 0.75, 48);
  REQUIRE(tbl.load_factor == 0.75);
  REQUIRE(tbl.entries_cap == 48);

  std::unordered_map<std::wstring, std::string> reference;
  for (int i = 0; i < 5000; ++i) {
    std::wstring key = std::to_wstring(i * 5201);
    std::string value = std::to_string(i);
    reference[key] = value;
    free(bjvm_hash_table_insert(&tbl, key.c_str(), -1, strdup(value.c_str())));
    free(bjvm_hash_table_insert(&tbl, key.c_str(), -1, strdup(value.c_str())));
  }
  for (int i = 1; i <= 4999; i += 2) {
    std::wstring key = std::to_wstring(i * 5201);
    free(bjvm_hash_table_delete(&tbl, key.c_str(), -1));
  }
  REQUIRE(tbl.entries_count == 2500);
  for (int i = 1; i <= 4999; i += 2) {
    std::wstring key = std::to_wstring(i * 5201);
    void *lookup = bjvm_hash_table_lookup(&tbl, key.c_str(), -1);
    REQUIRE(lookup == nullptr);
    std::string value = std::to_string(i);
    free(bjvm_hash_table_insert(&tbl, key.c_str(), -1, strdup(value.c_str())));
  }

  for (int i = 0; i < 5000; i += 2) {
    std::wstring key = std::to_wstring(i * 5201);
    void *value = bjvm_hash_table_lookup(&tbl, key.c_str(), -1);
    REQUIRE(value != nullptr);
    REQUIRE(reference[key] == (const char *)value);
  }

  bjvm_free_hash_table(tbl);
}

TEST_CASE("SignaturePolymorphic methods found") {
  // TODO
}

TEST_CASE("Malformed classfiles") {
  // TODO
}

#if 0
TEST_CASE("Class circularity error") {
  const char* cp[2] = { "test_files/circularity/", nullptr };
  bjvm_vm *vm = create_vm(false, cp);

  bjvm_thread_options options;
  bjvm_fill_default_thread_options(&options);
  bjvm_thread *thr = bjvm_create_thread(vm, options);

  bjvm_utf8 Main = bjvm_make_utf8(L"Main");
  bootstrap_class_create(thr, Main);
  free_utf8(Main);

  bjvm_free_thread(thr);
  bjvm_free_vm(vm);
}
#endif