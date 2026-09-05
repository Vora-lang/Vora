/**
 * @file builtins.h
 * @brief Unified registry of all built-in native functions.
 *
 * All user-facing built-in natives (print, type, len, clock, assert, int,
 * float, range, input, bin, oct, hex, error, Error, is_error, stackTrace,
 * import), standard library module registrations (math, json, fs, os,
 * datetime, regex), and collection method factories (Array, String, Dict,
 * Set, Map) are declared here.  Both the VM execution engine and the test
 * harness share this single source of truth.
 */

#pragma once

#include <memory>
#include <string>

#include "../gc/gc_ptr.h"

namespace vora {

class VM;
struct Array;
struct Dict;
struct Set;
struct Map;
class NativeFunction;

/**
 * @brief Register all core built-in native functions on a VM.
 *
 * Installs `print`, `type`, `len`, `clock`, `assert`, `int`, `float`,
 * `range`, `input`, `bin`, `oct`, `hex`, `error`, `Error`, `is_error`,
 * `stackTrace`, `import`, `vora_version`, and `_vora_len` into the VM's
 * global table.  Call after initGlobals() so global slots are pre-allocated.
 *
 * @param vm The VM instance to register functions into.
 */
void registerBuiltins(VM& vm);

/**
 * @brief Register math built-in native functions.
 *
 * Installs `sqrt`, `pow`, `abs`, `ceil`, `floor`, `round`, `sin`,
 * `cos`, `tan`, `log`, `log2`, `log10`, `exp`, `max`, `min`,
 * `random`, `seed`, `PI`, `E`, and trigonometry helpers.
 * Used by std/math.va.
 *
 * @param vm The VM instance to register functions into.
 */
void registerMathBuiltins(VM& vm);

/**
 * @brief Register JSON built-in native functions.
 *
 * Installs `json_parse` and `json_stringify`.  Used by std/json.va.
 *
 * @param vm The VM instance to register functions into.
 */
void registerJsonBuiltins(VM& vm);

/**
 * @brief Register filesystem built-in native functions.
 *
 * Installs `file_read`, `file_write`, `file_exists`, `file_remove`,
 * `file_size`, `file_list`, `is_dir`, `make_dir`, `remove_dir`,
 * and `file_copy`.  Used by std/fs.va.
 *
 * @param vm The VM instance to register functions into.
 */
void registerFsBuiltins(VM& vm);

/**
 * @brief Register OS-level built-in native functions.
 *
 * Installs `os_name`, `os_arch`, `os_version`, `os_env`, `os_setenv`,
 * `os_unsetenv`, `os_cwd`, `os_chdir`, `os_exec`, `os_exit`,
 * `os_sleep`, `os_tmpdir`, `os_homedir`, `os_args`, and platform helpers.
 * Used by std/os.va.
 *
 * @param vm The VM instance to register functions into.
 */
void registerOsBuiltins(VM& vm);

/**
 * @brief Register datetime built-in native functions.
 *
 * Installs `datetime_now`, `datetime_format`, `datetime_parse`,
 * `datetime_add`, `datetime_diff`, `datetime_from_parts`, `datetime_parts`,
 * and `datetime_timestamp`.  Used by std/datetime.va.
 *
 * @param vm The VM instance to register functions into.
 */
void registerDatetimeBuiltins(VM& vm);

/**
 * @brief Register regex built-in native functions.
 *
 * Installs `regex_match`, `regex_search`, `regex_replace`, and
 * `regex_split`.  Used by std/regex.va.
 *
 * @param vm The VM instance to register functions into.
 */
void registerRegexBuiltins(VM& vm);

/**
 * @brief Set the program argument list for os.args().
 *
 * Called from main() before registerOsBuiltins().  Stores argc/argv
 * globally so os.args() returns them at runtime.
 *
 * @param argc Argument count (including program name).
 * @param argv Argument vector.
 */
void setProgramArgs(int argc, char* argv[]);

/**
 * @brief Array method factory.
 *
 * Looks up a named method (push, pop, insert, remove, indexOf, contains,
 * sort, reverse, find, filter, map, reduce, slice, join, length, clear,
 * clone) and returns a NativeFunction callable bound to the given array.
 * Used by OP_GET_PROPERTY dispatch in the VM.
 *
 * @param name Method name.
 * @param arr  The target array instance.
 * @return A NativeFunction bound to @p arr, or nullptr if the method name
 *         is unknown.
 */
GcPtr<NativeFunction> getArrayMethod(
    const std::string& name,
    GcPtr<Array> arr);

/**
 * @brief String method factory.
 *
 * Looks up a named method (length, upper, lower, trim, startsWith,
 * endsWith, contains, indexOf, lastIndexOf, substr, replace, split)
 * and returns a NativeFunction callable bound to the given string.
 * Used by OP_GET_PROPERTY dispatch in the VM.
 *
 * @param name Method name.
 * @param str  The target string (by value — ownership transferred).
 * @return A NativeFunction bound to @p str, or nullptr if the method name
 *         is unknown.
 */
GcPtr<NativeFunction> getStringMethod(
    const std::string& name,
    std::string str);

/**
 * @brief Dict method factory.
 *
 * Looks up a named method (keys, values, has, remove, clear, size, clone)
 * and returns a NativeFunction callable bound to the given dict.
 * Used by OP_GET_PROPERTY dispatch in the VM.
 *
 * @param name Method name.
 * @param dict The target dict instance.
 * @return A NativeFunction bound to @p dict, or nullptr if the method name
 *         is unknown.
 */
GcPtr<NativeFunction> getDictMethod(
    const std::string& name,
    GcPtr<Dict> dict);

/**
 * @brief Set method factory.
 *
 * Looks up a named method (add, has, remove, clear, size, values, clone,
 * union, intersect, diff) and returns a NativeFunction callable bound to
 * the given set.  Used by OP_GET_PROPERTY dispatch in the VM.
 *
 * @param name Method name.
 * @param set  The target set instance.
 * @return A NativeFunction bound to @p set, or nullptr if the method name
 *         is unknown.
 */
GcPtr<NativeFunction> getSetMethod(
    const std::string& name,
    GcPtr<Set> set);

/**
 * @brief Map method factory.
 *
 * Looks up a named method (set, get, has, remove, clear, size, keys,
 * values, entries, clone) and returns a NativeFunction callable bound to
 * the given map.  Used by OP_GET_PROPERTY dispatch in the VM.
 *
 * @param name Method name.
 * @param map  The target map instance.
 * @return A NativeFunction bound to @p map, or nullptr if the method name
 *         is unknown.
 */
GcPtr<NativeFunction> getMapMethod(
    const std::string& name,
    GcPtr<Map> map);

} // namespace vora
