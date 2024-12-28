#include "../src/classpath.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Classpath creation") {
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