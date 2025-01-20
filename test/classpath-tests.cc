#include "../src/classpath.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Basic classpath operations", "[classpath]") {
  bjvm_classpath cp;
  char *error =
      bjvm_init_classpath(&cp, STR("test_files/broken_jar1/this_is_a_jar.jar"));
  REQUIRE(error != nullptr);
  free(error);
  bjvm_free_classpath(&cp);

  char *error2 = bjvm_init_classpath(&cp, STR("test_files/intact_jar/ok.jar"));
  REQUIRE(error2 == nullptr);
  uint8_t *bytes;
  size_t len;
  int ret_val = bjvm_lookup_classpath(&cp, STR("Egg.class"), &bytes, &len);
  REQUIRE(bytes != nullptr);
  REQUIRE(ret_val == 0);
  free(bytes);
  ret_val = bjvm_lookup_classpath(&cp, STR("Chicken.class"), &bytes, &len);
  REQUIRE(bytes != nullptr);
  REQUIRE(ret_val == 0);
  free(bytes);
  ret_val = bjvm_lookup_classpath(&cp, STR("Dog.class"), &bytes, &len);
  REQUIRE(bytes == nullptr);
  REQUIRE(ret_val == -1);
  bjvm_free_classpath(&cp);
}

TEST_CASE("Folder in classpath", "[classpath]") {
  bjvm_classpath cp;
  char *error = bjvm_init_classpath(
      &cp,
      STR("./jdk23.jar:test_files/circularity:test_files/classpath_test"));
  REQUIRE(error == nullptr);

  uint8_t *bytes;
  size_t len;
  int ret_val =
      bjvm_lookup_classpath(&cp, STR("jdk/internal/misc/Unsafe.class"), &bytes, &len);
  REQUIRE(bytes != nullptr);
  REQUIRE(ret_val == 0);
  free(bytes);
  ret_val = bjvm_lookup_classpath(&cp, STR("Chick.class"), &bytes, &len);
  REQUIRE(bytes != nullptr);
  REQUIRE(ret_val == 0);
  free(bytes);
  ret_val =
      bjvm_lookup_classpath(&cp, STR("nested/boi/Boi.class"), &bytes, &len);
  REQUIRE(bytes != nullptr);
  REQUIRE(ret_val == 0);
  free(bytes);
  ret_val = bjvm_lookup_classpath(&cp, STR("../classpath_test/Chick.class"),
                                  &bytes, &len);
  REQUIRE(bytes == nullptr);
  REQUIRE(ret_val == -1);
  free(bytes);

  bjvm_free_classpath(&cp);

  BENCHMARK("init classpath") {
    (void)bjvm_init_classpath(&cp,
                              STR("./jdk23.jar:test_files/"
                                  "circularity:test_files/classpath_test"));
    bjvm_free_classpath(&cp);
  };
}