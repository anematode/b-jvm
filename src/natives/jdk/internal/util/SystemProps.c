
#include <natives.h>
#include "cached_classdescs.h"

#include <unistd.h>

// COPIED FROM https://github.com/openjdk/jdk/blob/jdk23/src/java.base/share/classes/jdk/internal/util/SystemProps.java
enum {
  _display_country_NDX = 0,
  _display_language_NDX = 1 + _display_country_NDX,
  _display_script_NDX = 1 + _display_language_NDX,
  _display_variant_NDX = 1 + _display_script_NDX,
  _file_encoding_NDX = 1 + _display_variant_NDX,
  _file_separator_NDX = 1 + _file_encoding_NDX,
  _format_country_NDX = 1 + _file_separator_NDX,
  _format_language_NDX = 1 + _format_country_NDX,
  _format_script_NDX = 1 + _format_language_NDX,
  _format_variant_NDX = 1 + _format_script_NDX,
  _ftp_nonProxyHosts_NDX = 1 + _format_variant_NDX,
  _ftp_proxyHost_NDX = 1 + _ftp_nonProxyHosts_NDX,
  _ftp_proxyPort_NDX = 1 + _ftp_proxyHost_NDX,
  _http_nonProxyHosts_NDX = 1 + _ftp_proxyPort_NDX,
  _http_proxyHost_NDX = 1 + _http_nonProxyHosts_NDX,
  _http_proxyPort_NDX = 1 + _http_proxyHost_NDX,
  _https_proxyHost_NDX = 1 + _http_proxyPort_NDX,
  _https_proxyPort_NDX = 1 + _https_proxyHost_NDX,
  _java_io_tmpdir_NDX = 1 + _https_proxyPort_NDX,
  _line_separator_NDX = 1 + _java_io_tmpdir_NDX,
  _os_arch_NDX = 1 + _line_separator_NDX,
  _os_name_NDX = 1 + _os_arch_NDX,
  _os_version_NDX = 1 + _os_name_NDX,
  _path_separator_NDX = 1 + _os_version_NDX,
  _socksNonProxyHosts_NDX = 1 + _path_separator_NDX,
  _socksProxyHost_NDX = 1 + _socksNonProxyHosts_NDX,
  _socksProxyPort_NDX = 1 + _socksProxyHost_NDX,
  _stderr_encoding_NDX = 1 + _socksProxyPort_NDX,
  _stdout_encoding_NDX = 1 + _stderr_encoding_NDX,
  _sun_arch_abi_NDX = 1 + _stdout_encoding_NDX,
  _sun_arch_data_model_NDX = 1 + _sun_arch_abi_NDX,
  _sun_cpu_endian_NDX = 1 + _sun_arch_data_model_NDX,
  _sun_cpu_isalist_NDX = 1 + _sun_cpu_endian_NDX,
  _sun_io_unicode_encoding_NDX = 1 + _sun_cpu_isalist_NDX,
  _sun_jnu_encoding_NDX = 1 + _sun_io_unicode_encoding_NDX,
  _sun_os_patch_level_NDX = 1 + _sun_jnu_encoding_NDX,
  _user_dir_NDX = 1 + _sun_os_patch_level_NDX,
  _user_home_NDX = 1 + _user_dir_NDX,
  _user_name_NDX = 1 + _user_home_NDX,
  FIXED_LENGTH = 1 + _user_name_NDX,
};

static bjvm_handle *make_string_array(bjvm_thread *thread, int length) {
  bjvm_handle *arr = bjvm_make_handle(thread,
  CreateArray(thread, thread->vm->cached_classdescs->string->array_type, (int[]) { length }, 1));
  assert(arr->obj);
  return arr;
}

// TODO read the properties from the VM instead of hardcoding them
DECLARE_NATIVE("jdk/internal/util", SystemProps$Raw, platformProperties,
               "()[Ljava/lang/String;") {
  bjvm_handle *props = make_string_array(thread, FIXED_LENGTH);

  INIT_STACK_STRING(cwd, 1024);
  getcwd(cwd.chars, 1024);
  cwd.len = (int)strlen(cwd.chars);

  INIT_STACK_STRING(jre, 1024);
  jre = bprintf(jre, "%.*s/jre", fmt_slice(cwd));

#define SET_PROP(index, value) \
  { \
    bjvm_obj_header *str = bjvm_intern_string(thread, value); \
    ReferenceArrayStore(props->obj, index, str); \
  }

  SET_PROP(_file_encoding_NDX, STR("UTF-8"));
  SET_PROP(_stdout_encoding_NDX, STR("UTF-8"));
  SET_PROP(_stderr_encoding_NDX, STR("UTF-8"));
  SET_PROP(_file_separator_NDX, STR("/"));
  SET_PROP(_java_io_tmpdir_NDX, STR("/tmp"));
  SET_PROP(_line_separator_NDX, STR("\n"));
  SET_PROP(_path_separator_NDX, STR(":"));
  SET_PROP(_os_name_NDX, STR("Linux"));
  SET_PROP(_os_arch_NDX, STR("x86_64"));
  SET_PROP(_os_version_NDX, STR("5.4.0-1043-azure"));
  SET_PROP(_sun_arch_abi_NDX, STR("64"));
  SET_PROP(_sun_arch_data_model_NDX, STR("64"));
  SET_PROP(_sun_cpu_endian_NDX, STR("little"));
  SET_PROP(_sun_cpu_isalist_NDX, STR(""));
  SET_PROP(_sun_io_unicode_encoding_NDX, STR("UTF-8"));
  SET_PROP(_sun_jnu_encoding_NDX, STR("UTF-8"));
  SET_PROP(_sun_os_patch_level_NDX, STR("azure"));
  SET_PROP(_display_country_NDX, STR("US"));
  SET_PROP(_display_language_NDX, STR("en"));
  SET_PROP(_display_script_NDX, STR(""));
  SET_PROP(_display_variant_NDX, STR(""));
  SET_PROP(_format_country_NDX, STR("US"));
  SET_PROP(_format_language_NDX, STR("en"));
  SET_PROP(_format_script_NDX, STR(""));
  SET_PROP(_format_variant_NDX, STR(""));
  SET_PROP(_ftp_nonProxyHosts_NDX, STR(""));
  SET_PROP(_ftp_proxyHost_NDX, STR(""));
  SET_PROP(_ftp_proxyPort_NDX, STR(""));
  SET_PROP(_http_nonProxyHosts_NDX, STR(""));
  SET_PROP(_http_proxyHost_NDX, STR(""));
  SET_PROP(_http_proxyPort_NDX, STR(""));
  SET_PROP(_https_proxyHost_NDX, STR(""));
  SET_PROP(_https_proxyPort_NDX, STR(""));
  SET_PROP(_socksNonProxyHosts_NDX, STR(""));
  SET_PROP(_socksProxyHost_NDX, STR(""));
  SET_PROP(_socksProxyPort_NDX, STR(""));
  SET_PROP(_user_dir_NDX, cwd);
  SET_PROP(_user_name_NDX, STR("user"));
  SET_PROP(_user_home_NDX, STR("/home/user"));

  return (bjvm_stack_value){.obj = props->obj};
}

DECLARE_NATIVE("jdk/internal/util", SystemProps$Raw, vmProperties,
               "()[Ljava/lang/String;") {
  bjvm_handle *props = make_string_array(thread, 2);

  SET_PROP(0, STR("java.home"));
  char cwd[1024] = {0};
  getcwd(cwd, 1024);

  INIT_STACK_STRING(java_home, 1024);
  java_home = bprintf(java_home, "%s/jdk23", cwd);

  SET_PROP(1, java_home);

  return (bjvm_stack_value){.obj = props->obj};
}