#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include "doctest/doctest.h"

#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <unordered_map>

#include "tests-common.h"
#include <adt.h>
#include <analysis.h>
#include <bjvm.h>
#include <numeric>
#include <roundrobin_scheduler.h>
#include <unistd.h>
#include <util.h>

using namespace Bjvm::Tests;

double get_time() {
#ifdef EMSCRIPTEN
  return emscripten_get_now();
#else
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
#endif
}

TEST_CASE("Test STR() macro") {
  slice utf = STR("abc");
  REQUIRE(utf.chars[0] == 'a');
  REQUIRE(utf.chars[1] == 'b');
  REQUIRE(utf.chars[2] == 'c');
  REQUIRE(utf.len == 3);
}

TEST_CASE("Compressed bitset") {
  for (int size = 1; size < 256; ++size) {
    std::vector<u8> reference(size);
    compressed_bitset bitset;
    init_compressed_bitset(&bitset, size);

    for (int i = 0; i < 1000; ++i) {
      int index = rand() % size;
      switch (rand() % 4) {
      case 0: {
        int *set_bits = nullptr;
        list_compressed_bitset_bits(bitset, &set_bits);
        for (int i = 0; i < arrlen(set_bits); ++i) {
          if (!reference[set_bits[i]])
            REQUIRE(false);
        }
        arrfree(set_bits);
        break;
      }
      case 1: {
        bool test = test_set_compressed_bitset(&bitset, index);
        if (test != reference[index])
          REQUIRE(test == reference[index]);
        reference[index] = true;
        break;
      }
      case 2: {
        bool test = test_reset_compressed_bitset(&bitset, index);
        if (test != reference[index])
          REQUIRE(test == reference[index]);
        reference[index] = false;
        break;
      }
      case 3: {
        bool test = test_compressed_bitset(bitset, index);
        if (test != reference[index])
          REQUIRE(test == reference[index]);
        break;
      }
      }
    }

    free_compressed_bitset(bitset);
  }
}

TEST_CASE("parse_field_descriptor valid cases") {
  const char *fields = "Lcom/example/Example;[I[[[JLjava/lang/String;[[Ljava/lang/Object;BVCZ";
  field_descriptor com_example_Example, Iaaa, Jaa, java_lang_String, java_lang_Object, B, V, C, Z;
  arena arena;
  arena_init(&arena);

  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &com_example_Example, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &Iaaa, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &Jaa, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &java_lang_String, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &java_lang_Object, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &B, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &V, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &C, &arena));
  REQUIRE(!parse_field_descriptor(&fields, strlen(fields), &Z, &arena));

  REQUIRE(utf8_equals(com_example_Example.class_name, "com/example/Example"));
  REQUIRE(com_example_Example.dimensions == 0);
  REQUIRE(com_example_Example.base_kind == TYPE_KIND_REFERENCE);

  REQUIRE(Iaaa.base_kind == TYPE_KIND_INT);
  REQUIRE(Iaaa.dimensions == 1);

  REQUIRE(Jaa.base_kind == TYPE_KIND_LONG);
  REQUIRE(Jaa.dimensions == 3);

  REQUIRE(utf8_equals(java_lang_String.class_name, "java/lang/String"));
  REQUIRE(java_lang_String.dimensions == 0);

  REQUIRE(utf8_equals(java_lang_Object.class_name, "java/lang/Object"));
  REQUIRE(java_lang_Object.dimensions == 2);

  REQUIRE(B.base_kind == TYPE_KIND_BYTE);
  REQUIRE(C.base_kind == TYPE_KIND_CHAR);
  REQUIRE(V.base_kind == TYPE_KIND_VOID);
  REQUIRE(Z.base_kind == TYPE_KIND_BOOLEAN);

  arena_uninit(&arena);
}

TEST_CASE("String hash table") {
  string_hash_table tbl = make_hash_table(free, 0.75, 48);
  REQUIRE(tbl.load_factor == 0.75);
  REQUIRE(tbl.entries_cap == 48);

  std::unordered_map<std::string, std::string> reference;
  for (int i = 0; i < 5000; ++i) {
    std::string key = std::to_string(i * 5201);
    std::string value = std::to_string(i);
    reference[key] = value;
    free(hash_table_insert(&tbl, key.c_str(), -1, (void *)strdup(value.c_str())));
    free(hash_table_insert(&tbl, key.c_str(), -1, (void *)strdup(value.c_str())));
  }
  for (int i = 1; i <= 4999; i += 2) {
    std::string key = std::to_string(i * 5201);
    free(hash_table_delete(&tbl, key.c_str(), -1));
  }
  REQUIRE(tbl.entries_count == 2500);
  for (int i = 1; i <= 4999; i += 2) {
    std::string key = std::to_string(i * 5201);
    void *lookup = hash_table_lookup(&tbl, key.c_str(), -1);
    REQUIRE(lookup == nullptr);
    std::string value = std::to_string(i);
    free(hash_table_insert(&tbl, key.c_str(), -1, strdup(value.c_str())));
  }

  for (int i = 0; i < 5000; i += 2) {
    std::string key = std::to_string(i * 5201);
    void *value = hash_table_lookup(&tbl, key.c_str(), -1);
    REQUIRE(value != nullptr);
    REQUIRE(reference[key] == (const char *)value);
  }

  free_hash_table(tbl);
}

TEST_CASE("SignaturePolymorphic methods found") {
  // TODO
}

TEST_CASE("Malformed classfiles") {
  // TODO
}

TEST_CASE("Interning") {
  auto result = run_test_case("test_files/interning/");
  REQUIRE(result.stdout_ == "false\nfalse\ntrue\ntrue\ntrue\nfalse\ntrue\nfalse\n");
}

TEST_CASE("NegativeArraySizeException") {
  auto result = run_test_case("test_files/negative_array_size/");
  std::string expected = "abcdefghij";
  REQUIRE(result.stdout_ == expected);
}

TEST_CASE("System.arraycopy") {
  auto result = run_test_case("test_files/arraycopy/");
  std::string expected = "abcdefghijkljklm";
  REQUIRE(result.stdout_ == expected);
}

TEST_CASE("Passing long to method calls") {
  auto result = run_test_case("test_files/long_calls/");
  std::string expected = "abcdabcd";
  REQUIRE(result.stdout_ == expected);
}

TEST_CASE("Big decimal #1") {
  std::string expected = R"(10.5 + 2.3 = 12.8
10.5 - 2.3 = 8.2
10.5 * 2.3 = 24.15
10 / 3 (rounded to 2 decimals) = 3.33
10.5 equals 10.500: false
10.5 compareTo 10.500: 0
pi * pi is 9.869604401089358618834490999876151135199537704046847772071781337378379488504041
)";

  for (int i = 0; i < 10; ++i)
    expected += expected; // repeat 1024 times
  auto result = run_test_case("test_files/bench_big_decimal/");
  REQUIRE(result.stdout_ == expected);
}

TEST_CASE("Math natives") {
  auto result = run_test_case("test_files/math/", true);
  REQUIRE(result.stdout_ == "abcdefghijklmnopqrstu");
}

TEST_CASE("Signature polymorphism") {
  auto result = run_test_case("test_files/signature_polymorphism/", true);
  REQUIRE(result.stdout_ == R"(nanny
savvy
Hello, world.
)");
}

TEST_CASE("Null getfield putfield") {
  auto result = run_test_case("test_files/null_getfield_putfield/", true);
  REQUIRE(result.stdout_ == R"(src is:
byte: 1
short: 1
int: 2
long: 1
float: 1.0
double: 1.0
char: a
bool: true
obj: hello
original int val: 2
copied int val: 2
Cannot assign field "byteVal" because "<parameter2>" is null
Cannot assign field "shortVal" because "<parameter2>" is null
Cannot assign field "intVal" because "<parameter2>" is null
Cannot assign field "longVal" because "<parameter2>" is null
Cannot assign field "floatVal" because "<parameter2>" is null
Cannot assign field "doubleVal" because "<parameter2>" is null
Cannot assign field "charVal" because "<parameter2>" is null
Cannot assign field "boolVal" because "<parameter2>" is null
Cannot assign field "objVal" because "<parameter2>" is null
Cannot read field "byteVal" because "<parameter1>" is null
Cannot read field "shortVal" because "<parameter1>" is null
Cannot read field "intVal" because "<parameter1>" is null
Cannot read field "longVal" because "<parameter1>" is null
Cannot read field "floatVal" because "<parameter1>" is null
Cannot read field "doubleVal" because "<parameter1>" is null
Cannot read field "charVal" because "<parameter1>" is null
Cannot read field "boolVal" because "<parameter1>" is null
Cannot read field "objVal" because "<parameter1>" is null
result is:
byte: 0
short: 0
int: 0
long: 0
float: 0.0
double: 0.0
char: e
bool: false
obj: null
result int val: 0
result obj val: null
)");
}

TEST_CASE("N-body problem") {
  auto result = run_test_case("test_files/n_body_problem/", false, "NBodyProblem");

  //  REQUIRE(result.stdout_.find("Running Simulation...") != std::string::npos);
  //  REQUIRE(result.stdout_.find("Roughly Equals: true") != std::string::npos);
  //  REQUIRE(result.stdout_.find("Energy change minimal: true") != std::string::npos);
  //  REQUIRE(result.stdout_.find("Sun final position: ") != std::string::npos);
}

TEST_CASE("Basic lambda") {
  auto result = run_test_case("test_files/basic_lambda/", true);

  REQUIRE(result.stdout_ == R"(Hello from lambda!
variable passed in
2eggs
Hello from lambda!
variable passed in
2eggs
)");
}

TEST_CASE("Advanced lambda") {
  std::string expected = R"(10 + 5 = 15
Sum of numbers: 15
1
2
3
4
5
Incremented: 2
Incremented: 4
Incremented: 6
Incremented: 8
Incremented: 10
Sum of lots of numbers: 499500
Cow value: 15
)";
  for (int i = 0; i < 10; ++i)
    expected += expected;
  auto result = run_test_case("test_files/advanced_lambda/", true);
  // REQUIRE(result.stdout_.length() == expected.length());
  REQUIRE(result.stdout_ == expected);
}

TEST_CASE("Class<?> implementation") {
  auto result = run_test_case("test_files/reflection_class/", true);
  REQUIRE(result.stdout_ ==
          R"(int1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
boolean1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
byte1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
char1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
void1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
short1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
long1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
float1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
double1041nullfalsefalsefalsefalsefalsefalse00nulltruenull
java.lang.Integer17nullfalsefalsetruefalsefalsefalse861class java.lang.Numberfalsenull
[Ljava.lang.Integer;1041class java.lang.Integertruefalsefalsetruefalsefalse00class java.lang.Objectfalsenull
[[Ljava.lang.Integer;1041class [Ljava.lang.Integer;truefalsefalsefalsefalsefalse00class java.lang.Objectfalsenull
[I1041inttruefalsefalsefalsetruefalse00class java.lang.Objectfalsenull
[[I1041class [Itruefalsefalsefalsefalsetrue00class java.lang.Objectfalsenull
[C1041chartruefalsefalsefalsefalsefalse00class java.lang.Objectfalsenull
[[C1041class [Ctruefalsefalsefalsefalsefalse00class java.lang.Objectfalsenull
java.io.Serializable1537nullfalsetruetruetruetruetrue00nullfalsenull
)");
}

TEST_CASE("NPE") {
  auto result = run_test_case("test_files/npe/", true);
  REQUIRE(result.stdout_ == "abcdefghijklmnopqr");
}

TEST_CASE("ArithmeticException") {
  auto result = run_test_case("test_files/arithmetic_exception/", true);
  REQUIRE(result.stdout_ == "abcd");
}

TEST_CASE("Numeric conversions") {
  auto result = run_test_case("test_files/numerics/", true);
  REQUIRE(result.stdout_ == R"(3.4028235E38 -> 2147483647
1.4E-45 -> 0
Infinity -> 2147483647
-Infinity -> -2147483648
NaN -> 0
3.4028235E38 -> 9223372036854775807L
1.4E-45 -> 0L
Infinity -> 9223372036854775807L
-Infinity -> -9223372036854775808L
NaN -> 0L
1.7976931348623157E308 -> 2147483647
4.9E-324 -> 0
Infinity -> 2147483647
-Infinity -> -2147483648
NaN -> 0
1.7976931348623157E308 -> 9223372036854775807L
4.9E-324 -> 0L
Infinity -> 9223372036854775807L
-Infinity -> -9223372036854775808L
NaN -> 0L
)");
}

TEST_CASE("ClassCircularityError") {
  auto result = run_test_case("test_files/circularity/", true);
  REQUIRE(result.stdout_ == "abc");
}

TEST_CASE("Array creation doesn't induce <clinit>") {
  auto result = run_test_case("test_files/array_clinit/", true);
  REQUIRE(result.stdout_ == "Hey :)\n");
}

TEST_CASE("Simple OutOfMemoryError") {
  auto result = run_test_case("test_files/out_of_memory/", true);
  REQUIRE(result.stderr_.find("OutOfMemoryError") != std::string::npos);
}

TEST_CASE("Exceptions in <clinit>") {
  auto result = run_test_case("test_files/eiie/", true);
  REQUIRE(result.stdout_ == R"(Egg
Chicken
)");
}

TEST_CASE("ConstantValue initialisation") {
  auto result = run_test_case("test_files/constant_value/", true);
  REQUIRE(result.stdout_ == "2147483647\n");
}

TEST_CASE("Deranged CFG") {
  auto result = run_test_case("test_files/cfg_fuck/", true);
  auto expected = ReadFile("test_files/cfg_fuck/reference.txt").value();
  std::string as_string;
  for (unsigned char i : expected)
    as_string.push_back(i);
  REQUIRE(result.stdout_ == as_string);
}

TEST_CASE("Immediate dominators computation on cursed CFG") {
  classdesc desc;
  auto cursed_file = ReadFile("test_files/cfg_fuck/Main.class").value();
  parse_classfile(cursed_file.data(), cursed_file.size(), &desc, nullptr);
  REQUIRE(utf8_equals(desc.name, "Main"));

  cp_method *m = desc.methods + 4;
  REQUIRE(utf8_equals(m->name, "main"));

  analyze_method_code(m, nullptr);
  auto *analy = (code_analysis *)m->code_analysis;
  scan_basic_blocks(m->code, analy);
  compute_dominator_tree(analy);

  std::vector<std::pair<int, u32>> doms = {{1, 0},   {2, 1},  {3, 2},  {4, 3},  {5, 4},   {6, 5},  {7, 6},   {8, 6},
                                           {9, 6},   {10, 6}, {11, 6}, {12, 6}, {13, 6},  {14, 5}, {15, 14}, {16, 14},
                                           {17, 16}, {18, 5}, {19, 4}, {20, 4}, {21, 20}, {22, 1}};

  for (auto [a, b] : doms) {
    REQUIRE(analy->blocks[a].idom == b);
  }

  int result = attempt_reduce_cfg(analy);
  REQUIRE(result == 0);

  cp_method *m2 = desc.methods + 5;
  analyze_method_code(m2, nullptr);

  free_classfile(desc);
}

TEST_CASE("Conflicting defaults") {
  // Attempting to invokeinterface on a class which inherits multiple maximally
  // -specific implementations of a given interface method.
  auto result = run_test_case("test_files/conflicting_defaults/", true, "ConflictingDefaults");
  REQUIRE(result.stderr_.find("AbstractMethodError") != std::string::npos);
}

TEST_CASE("Records") {
  auto result = run_test_case("test_files/records/", true, "Records");
  REQUIRE(result.stdout_ == R"(true
true
)");
}

#if 0
TEST_CASE("JSON tests") {
  std::string classpath =
      "test_files/json:test_files/json/gson-2.11.0.jar:test_files/"
      "json/jackson-core-2.18.2.jar:test_files/json/"
      "jackson-annotations-2.18.2.jar:test_files/json/"
      "jackson-databind-2.18.2.jar";
  auto result = run_test_case(classpath, true, "GsonExample");
  REQUIRE_THAT(result.stdout_, Equals(R"(Student: Goober is 21 years old.
{"name":"Goober","age":21}
{"name":"Goober","age":21})"));
}
#endif

TEST_CASE("ArrayStoreException") {
  auto result = run_test_case("test_files/array_store/", true, "ArrayStore");
  REQUIRE(result.stderr_ == "java.lang.ArrayStoreException: Bus\n\tat "
                            "ArrayStore.main(ArrayStore.java:10)\n");
}

TEST_CASE("Random API") {
  auto result = run_test_case("test_files/random/", true, "RandomTest");
  REQUIRE(result.stdout_.find("Random Integer (50 to 150)") != std::string::npos); // Just check that it completes :)
}

TEST_CASE("Simulated input/output") {
  // auto human_result = run_test_case("test_files/system_input/", false);
  std::cout << "test one char" << std::endl;
  auto result_char = run_test_case("test_files/system_input/", true, "Main", "A");
  REQUIRE(result_char.stdout_ == R"(Write a byte
Data read: 65
As a char: A
)");
  REQUIRE(result_char.stdin_ == "");

  std::cout << "test no char" << std::endl;
  auto end_of_file = run_test_case("test_files/system_input/", true, "Main", "");
  REQUIRE(end_of_file.stdout_ == R"(Write a byte
EOF
)");
  REQUIRE(end_of_file.stdin_ == "");

  std::cout << "test many char" << std::endl;
  auto result_many = run_test_case("test_files/system_input/", true, "Main", "ABCDEFG");
  REQUIRE(result_many.stdout_ == R"(Write a byte
Data read: 65
As a char: A
)");
  REQUIRE(result_many.stdin_ == ""); // BufferedReader tries to consume 8192 bytes, but we only provide 7
}

#if 0 // these cases are slowwww

TEST_CASE("Sudoku solver") {
  int num_puzzles = 33761;
  std::cout << "Starting sudoku solver" << std::endl;
  std::cout << "Hang on tight, solving " << num_puzzles << " sudoku puzzles..." << std::endl;
  auto now = std::chrono::system_clock::now();

  auto result = run_test_case("test_files/sudoku/", true, "Main");
  // last puzzle
  REQUIRE(result.stdout_.find("649385721218674359357291468495127836163948572782536194876452913531869247924713685") != std::string::npos);

  auto end = std::chrono::system_clock::now();
  long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
  std::cout << "Done in " << elapsed << " ms!" << std::endl;
  std::cout << "That's " << (double) elapsed / num_puzzles << " ms per puzzle!" << std::endl;
}

TEST_CASE("Scheduled sudoku solver") {
  int num_puzzles = 33761;
  std::cout << "Starting sudoku solver with a scheduler" << std::endl;
  std::cout << "Hang on tight, solving " << num_puzzles << " sudoku puzzles..." << std::endl;
  auto now = std::chrono::system_clock::now();

  auto result = run_scheduled_test_case("test_files/sudoku/", true, "Main");
  // last puzzle
  REQUIRE(result.stdout_.find("649385721218674359357291468495127836163948572782536194876452913531869247924713685") != std::string::npos);
  REQUIRE(result.sleep_count == 0); // chop chop

  auto end = std::chrono::system_clock::now();
  long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
  std::cout << "Scheduler yielded " << result.yield_count << " times" << std::endl;
  std::cout << "Done in " << elapsed << " ms!" << std::endl;
  std::cout << "That's " << (double) elapsed / num_puzzles << " ms per puzzle!" << std::endl;
}

TEST_CASE("Scheduled worker sudoku solver") {
  int num_puzzles = 33761;
  std::cout << "Starting sudoku solver with a worker thread" << std::endl;
  std::cout << "Hang on tight, solving " << num_puzzles << " sudoku puzzles..." << std::endl;
  auto now = std::chrono::system_clock::now();

  auto result = run_scheduled_test_case("test_files/sudoku/", false, "WorkerThreadSudoku");
  // last puzzle
  REQUIRE(result.stdout_.find("649385721218674359357291468495127836163948572782536194876452913531869247924713685") != std::string::npos);
  REQUIRE(result.sleep_count == 0); // chop chop

  auto end = std::chrono::system_clock::now();
  long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
  std::cout << "Scheduler yielded " << result.yield_count << " times" << std::endl;
  std::cout << "Done in " << elapsed << " ms!" << std::endl;
  std::cout << "That's " << (double) elapsed / num_puzzles << " ms per puzzle!" << std::endl;
}

TEST_CASE("Autodiff") {
  int num_derivatives = 10000 * 10 + 3;
  std::cout << "Testing Autodiff" << std::endl;
  std::cout << "Hang on tight, automatically differentiating " << num_derivatives << " simple expressions..." << std::endl;
  auto now = std::chrono::system_clock::now();

  auto result = run_test_case("test_files/autodiff/", true, "Main");
  // last test
  REQUIRE(result.stdout_.find("((84.02894029503324*(-(sin((x*y))*x)+1.0))+((84.02894029503324*x)*-(((cos((x*y))*x)*y)+sin((x*y))))) = -3209354.522523045 == -3209354.522523045")
          != std::string::npos);

  auto end = std::chrono::system_clock::now();
  long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
  std::cout << "Done in " << elapsed << " ms!" << std::endl;
  std::cout << "That's " << (double) elapsed / num_derivatives << " ms per evaluation!" << std::endl;
}

#endif

TEST_CASE("Method parameters reflection API") {
  auto result = run_test_case("test_files/method_parameters/", true, "MethodParameters");
  REQUIRE(result.stdout_ == R"(Parameter names for method 'exampleMethod':
param1
param2
param3
)");
}

TEST_CASE("Extended NPE message") {
  auto result = run_test_case("test_files/extended_npe/", false, "ExtendedNPETests");
  REQUIRE(result.stdout_ == "");
}

#if 0
TEST_CASE("ITextPDF") {
  auto result = run_test_case("test_files/pdf:test_files/pdf/itextpdf-5.5.13.4.jar"
    ":test_files/pdf/kernel-9.0.0.jar:test_files/pdf/layout-9.0.0.jar:test_files/pdf/io-9.0.0.jar:"
    "test_files/pdf/commons-9.0.0.jar:test_files/pdf/slf4j-api-2.0.16.jar", false, "PDFDemo");
}
#endif

TEST_CASE("Class loading") {
  auto result = run_test_case("test_files/basic_classloader/", false, "URLClassLoaderExample");
}

TEST_CASE("java.lang.reflect.Method") {
  auto result = run_test_case("test_files/reflection_method/", true, "ReflectionMethod");
  REQUIRE(result.stdout_ == R"(Reached!
Reached2!
aReached3!
Reached4!
b3030ef)");
}

TEST_CASE("Simple generic types") {
  auto result = run_test_case("test_files/generic_types/", true, "GenericBox");
  REQUIRE(result.stdout_ == R"(Integer Value: 10
String Value: Hello World
Box in Box Value: 10
Cursed time
Reached
Correctly threw ClassCastException upon getting
Done
)");
}

TEST_CASE("Playground") { auto result = run_test_case("test_files/compiler", false); }

#if 0
TEST_CASE("printf") {
  auto result = run_test_case("test_files/printf", true, "Printf");
  REQUIRE(result.stdout_ == R"(Hello!
Hello, world!
Hello, 42! The answer is 42. The quick brown fox jumps over the lazy dog.
)");
}
#endif

TEST_CASE("CloneNotSupportedException") {
  auto result = run_test_case("test_files/clone_not_supported/", true, "CloneNotSupportedTest");
  REQUIRE(result.stdout_ == R"(CloneNotSupportedException caught
NullPointerException caught
)");
}

#if 0
TEST_CASE("Filesystem") {
  auto result = run_test_case("test_files/filesystem", true, "Filesystem");
  REQUIRE_THAT(result.stderr_, Equals(""));
  REQUIRE_THAT(result.stdout_, Equals("UnixFileSystem"));
}
#endif

TEST_CASE("Multithreading") {
  auto result = run_scheduled_test_case("test_files/basic_multithreading/", false, "Multithreading");
  (void)result; // use this var fr
}

TEST_CASE("Monitor reentrancy") {
  auto result = run_scheduled_test_case("test_files/reentrant_monitor/", true, "Main");
  REQUIRE(result.stdout_ == R"(ReentrantFactorialCalculator: {}
5! = 120
ReentrantFactorialCalculator: {1=1, 2=2, 3=6, 4=24, 5=120}
3! = 6
ReentrantFactorialCalculator: {1=1, 2=2, 3=6, 4=24, 5=120}
100! = 2432902008176640000
ReentrantFactorialCalculator: {1=1, 2=2, 3=6, 4=24, 5=120, 6=720, 7=5040, 8=40320, 9=362880, 10=3628800, 11=39916800, 12=479001600, 13=6227020800, 14=87178291200, 15=1307674368000, 16=20922789888000, 17=355687428096000, 18=6402373705728000, 19=121645100408832000, 20=2432902008176640000}
caching correctly: true
)");
}

TEST_CASE("Single thread interruption") {
  auto result = run_scheduled_test_case("test_files/single_thread_interrupt/", true);

  REQUIRE(result.stdout_ == R"(initially interrupted: false
interrupted: true
interrupted: true
interrupted: false
slept for at least 1000 ms? true
interrupting
interrupted while sleeping
interrupted: false
slept for at least 1000 ms? false
)");

  REQUIRE(result.sleep_count == 1);
  REQUIRE(result.ms_slept <= 1000000);          // 1 second
  REQUIRE(1000000 - result.ms_slept <= 100000); // give or take 0.1 seconds
}

TEST_CASE("IllegalMonitorStateException") {
  auto result = run_test_case("test_files/illegal_monitor/", true, "IllegalMonitors");
  REQUIRE(result.stdout_ == R"(Caught exception1
null
Caught exception2
null
Caught exception3
null
Caught exception4
null
Caught exception5
null
Caught exception6
Cannot enter synchronized block because "<local0>" is null
Caught exception7
Cannot exit synchronized block because "<local0>" is null
)");
}

TEST_CASE("Concurrent initialisation") {
  auto result = run_scheduled_test_case("test_files/concurrent_initialisation/", true, "ConcurrentInitialization");
  REQUIRE(result.stdout_ == R"(Initializing...
Finished initializing!
)");
}

TEST_CASE("VarHandle") {
  auto result = run_test_case("test_files/var_handles/", true, "Main");
  REQUIRE(result.stdout_ == R"(true
new
)");
}

TEST_CASE("Random UUID") {
  auto result = run_test_case("test_files/random_uuid/", false, "Main");
}

TEST_CASE("New instance") {
  auto result = run_test_case("test_files/new_instance/", true, "Main");
  REQUIRE(result.stdout_ == R"(Test 3.14
Test
)");
}

TEST_CASE("URLClassLoader") {
  auto result = run_test_case("test_files/url-classloader/", true, "LoaderTest");
  REQUIRE(result.stdout_ == "Hello, world!\n");
}

TEST_CASE("frem and drem") {
  auto result = run_test_case("test_files/frem_drem/", true, "FremDremTest");
  REQUIRE(result.stdout_ == R"(0.099999994
0.09999999999999998
)");
}

TEST_CASE("Manually thrown exception") {
  auto result = run_test_case("test_files/manually_thrown", true, "ManuallyThrown");
  REQUIRE(result.stderr_ == "java.lang.NullPointerException\n\tat ManuallyThrown.cow(ManuallyThrown.java:11)\n\tat ManuallyThrown.main(ManuallyThrown.java:4)\n");
}