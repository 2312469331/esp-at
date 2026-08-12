---
name: esp-at-custom-cmd
description: Use when adding user-defined AT commands to ESP-AT firmware, defining custom AT command handlers (AT+XXX), registering AT commands, or creating an at_custom_cmd-style component. Covers the 6-step workflow (define, register, deps, link option, env var, compile) plus complex patterns: return messages, parameter parsing, optional parameters, blocking commands, and reading input data from the AT port.
---

# Adding User-Defined AT Commands to ESP-AT

Reference docs: `docs/zh_CN/Compile_and_Develop/How_to_add_user-defined_AT_commands.rst` (Chinese) and
`docs/en/Compile_and_Develop/How_to_add_user-defined_AT_commands.rst` (English).
The AT command-set source is closed-source; it ships as a prebuilt lib under `components/at/lib` and is
the parser that dispatches custom commands. The canonical example lives at `examples/at_custom_cmd/`.

## Core API / headers

- `#include "esp_at.h"` (umbrella), plus `esp_at_core.h`, `esp_at_types.h`, `esp_at_cmd_register.h` under `components/at/include/`.
- Command descriptor struct: `esp_at_cmd_t` with fields `name, test_fn, query_fn, set_fn, exe_fn`. Unsupported types set to `NULL`.
- Register: `esp_at_custom_cmd_array_register(const esp_at_cmd_t *cmds, uint32_t num)`.
- Auto-init macro: `ESP_AT_CMD_SET_INIT_FN(register_fn, priority)`.

## Step 1 — Define the command

File: `examples/at_custom_cmd/custom/at_custom_cmd.c` (+ header in `include/`), or new files under the `custom/` and `include/` dirs.

Naming rules: name starts with `+`; allowed chars: `A-Z a-z 0-9 ! % - . / : _`.
Each command can support up to 4 types: test (`AT+XXX=?`), query (`AT+XXX?`), set (`AT+XXX=...`), execute (`AT+XXX`).

```c
// 4 handler prototypes
static uint8_t at_test_cmd_foo(uint8_t *cmd_name);   // cmd_name = "+FOO"
static uint8_t at_query_cmd_foo(uint8_t *cmd_name);
static uint8_t at_setup_cmd_foo(uint8_t para_num);   // para_num = number of parsed parameters
static uint8_t at_exe_cmd_foo(uint8_t *cmd_name);

static const esp_at_cmd_t at_custom_cmd[] = {
    {"+TEST", at_test_cmd_test, at_query_cmd_test, at_setup_cmd_test, at_exe_cmd_test},
    {"+FOO", NULL, NULL, at_setup_cmd_foo, at_exe_cmd_foo},   // only set + exe types
};
```

Handlers return `ESP_AT_RESULT_CODE_OK` / `ESP_AT_RESULT_CODE_ERROR` (see `esp_at_rc_t` in `esp_at_types.h`).
Output via `esp_at_port_write_data(buf, len)`.

## Step 2 — Register function

Define `bool esp_at_custom_cmd_register(void)` calling `esp_at_custom_cmd_array_register(...)`, then initialize it:

```c
bool esp_at_custom_cmd_register(void)
{
    return esp_at_custom_cmd_array_register(at_custom_cmd, sizeof(at_custom_cmd) / sizeof(esp_at_cmd_t));
}

ESP_AT_CMD_SET_INIT_FN(esp_at_custom_cmd_register, 1);
```

If adding commands from a *new* file, do NOT reuse `esp_at_custom_cmd_register` (already defined in the example);
name it e.g. `esp_at_custom_cmd_register_foo` and initialize with `ESP_AT_CMD_SET_INIT_FN`.

## Step 3 — Component dependencies

Edit `examples/at_custom_cmd/CMakeLists.txt`. Default requires are `at freertos nvs_flash`.
If using e.g. `lwip`, set `set(require_components at freertos nvs_flash lwip)`.

## Step 4 — Link option (forced link of the register fn)

The register function is only referenced via the init macro, so force-link it to `${COMPONENT_LIB}` so it survives
garbage collection:

```cmake
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_at_custom_cmd_register")
```

Use the actual function name (e.g. `"-u esp_at_custom_cmd_register_foo"`).

## Step 5 — Component environment variable

The component must be visible to the ESP-IDF build via `AT_CUSTOM_COMPONENTS` (parsed in top-level `CMakeLists.txt`,
appended to `EXTRA_COMPONENT_DIRS`). Two methods:

1. CLI (local build):
   - Linux/macOS: `export AT_CUSTOM_COMPONENTS=(absolute_path_of_at_custom_cmd)`
   - Windows: `set AT_CUSTOM_COMPONENTS=(absolute_path_of_at_custom_cmd)`
   - Multiple: space-separated, e.g. `export AT_CUSTOM_COMPONENTS="~/a ~/b"`
2. In `build.py` `setup_env_variables()`:
   ```python
   at_custom_cmd_path = os.path.join(os.getcwd(), 'examples/at_custom_cmd')
   os.environ['AT_CUSTOM_COMPONENTS'] = at_custom_cmd_path
   ```

Note: editing components directly under `esp-at/components/` avoids step 5 but is discouraged.

## Step 6 — Compile

Local: `./build.py` or `./build.py save` (see `How_to_clone_project_and_compile_it` docs / `build.py --help`),
or web build. Then flash the firmware.

## Complex patterns

### Return messages
Use `esp_at_write_result(code)` to emit extra result codes (e.g. `ESP_AT_RESULT_CODE_SEND_OK` / `_SEND_FAIL`) in
addition to the handler's return value.

```c
uint8_t at_exe_cmd_test(uint8_t *cmd_name)
{
    ...
    send_data_to_server();
    esp_at_write_result(ESP_AT_RESULT_CODE_SEND_OK);
    return ESP_AT_RESULT_CODE_OK;
}
```

### Parameter parsing
- `esp_at_get_para_as_digit(index, &int32_t)` — numeric parameters.
- `esp_at_get_para_as_str(index, &uint8_t *)` — string parameters (remember to free if allocated).
- Parse status enum `esp_at_para_parse_ret_t` (in `esp_at_core.h`):
  `ESP_AT_PARA_PARSE_RET_OK` / `ESP_AT_PARA_PARSE_RET_FAIL` (-1) / `ESP_AT_PARA_PARSE_RET_OMITTED`.
  Deprecated aliases `ESP_AT_PARA_PARSE_RESULT_*` exist in `esp_at_legacy.h` (the example `at_custom_cmd.c` uses them).
- `esp_at_get_current_cmd_name()` returns the current `+XXX` name.
- Parameter index increments per successful parse. Empty string `""` counts as present, not omitted.

### Optional (omitted) parameters
CRITICAL: calling `esp_at_get_para_as_digit/str(index)` with an index that does NOT exist returns
`ESP_AT_PARA_PARSE_RET_FAIL` (-1), NOT `_OMITTED`. `_OMITTED` only means "empty field inside the parameter
string" (e.g. `AT+TEST=1,,3`). You MUST gate optional-parameter reads on `para_num` first, per the official docs.

- Middle/first param: parse, and treat `ESP_AT_PARA_PARSE_RET_OMITTED` as "user left a gap" (e.g. `AT+TEST=1,,3`).
- Last/trailing param: check `num_index != para_num` BEFORE calling the parse API; only parse when a param actually
  exists, otherwise keep the default value. Otherwise `AT+TEST=1,2` (no 3rd param) hits a non-existent index → FAIL.

```c
static uint8_t at_setup_cmd_foo(uint8_t para_num)
{
    int32_t a = 0, b = 0;
    uint8_t index = 0;

    if (esp_at_get_para_as_digit(index++, &a) != ESP_AT_PARA_PARSE_RET_OK) {
        return ESP_AT_RESULT_CODE_ERROR;
    }
    /* optional trailing param: only read when it actually exists */
    if (index != para_num) {
        esp_at_para_parse_ret_t ret = esp_at_get_para_as_digit(index++, &b);
        if (ret != ESP_AT_PARA_PARSE_RET_OK && ret != ESP_AT_PARA_PARSE_RET_OMITTED) {
            return ESP_AT_RESULT_CODE_ERROR;
        }
    }
    return ESP_AT_RESULT_CODE_OK;
}
```

Rule of thumb: ALWAYS consume required params with an incrementing index, then guard each optional param with
`index != para_num` before parsing it.

### Blocking a command
Use a binary semaphore (`xSemaphoreCreateBinary` / `xSemaphoreTake(sema, portMAX_DELAY)`), given by another task.

### Reading input data from the AT port
- `esp_at_port_enter_specific(callback)` — register callback; called when input data arrives.
- `esp_at_port_exit_specific()` — remove callback.
- `esp_at_port_read_data(buf, len)`, `esp_at_port_get_data_length()`, `esp_at_port_recv_data_notify(len, timeout)`.
- Write `">"` prompt first, then loop on a semaphore until the expected length is received (specified-length mode),
  or until `+++` is received (pass-through/unspecified-length mode), then handle leftover bytes with `esp_at_port_recv_data_notify`.

## Verification

Flash and run `AT+TEST` (or the new command):
- `AT+TEST=?` → test output + `OK`
- `AT+TEST?` → query output + `OK`
- `AT+TEST=1,"espressif"` → setup output + `OK`
- `AT+TEST` → execute output + `OK`

Common gotchas when testing on hardware:
- Setup (`=...`) commands silently returning `ERROR` with NO custom message almost always means parameter parsing
  failed. The likely causes, in order:
  1. Reading an optional/omitted parameter that isn't guarded by `para_num` (returns `_FAIL`). See the optional-params
     section above — this is the #1 mistake.
  2. Too-small buffer: `esp_at_port_write_data` writes `len` bytes where `len` is snprintf's *would-be* length even
     when truncated, so a help/echo string longer than the stack buffer reads past it and prints garbage.
- Don't register a command whose name is a prefix of another command (e.g. `+GPIO` + `+GPIOM`): the shorter one can
  shadow the longer ones. Use distinct command names or drop the short prefix command.
- If a read command calls `gpio_set_direction(pin, GPIO_MODE_INPUT)` unconditionally, it resets any previously
  configured output state, so reading back your own output level fails. Let a bare read preserve the current
  direction, and only switch to input when the user explicitly asks for pull-up/pull-down.
- Confirm which firmware is flashed: a `=?` test output changing behavior between tests means you're flashing old
  builds. Check the commit that produced the artifact before debugging.
- Always rebuild from clean state if `AT_CUSTOM_COMPONENTS` or CMakeLists changed.

## GPIO commands: per-target pin reservation

The example `at_custom_cmd.c` ships GPIO commands (`AT+GPIOM/GPIOW/GPIOR`) whose
pin-validity check (`at_gpio_is_valid`) must reserve the right pins per target:

- The framework macros `GPIO_IS_VALID_GPIO` / `GPIO_IS_VALID_OUTPUT_GPIO` already
  handle the per-target legal pin ranges (ESP32: 0-39; ESP32-C3: 0-10 and 18-21,
  11-17 do not exist). Only target-specific *reservations* are hand-coded under
  `#ifdef CONFIG_IDF_TARGET_*`.
- ESP32 (WROOM-32): reserve UART0 AT port TX=1/RX=3, SPI-flash GPIO6~11, strapping 0/2/5/12/15.
- ESP32-C3: reserve the AT UART port pins (ASK the user which — official default is
  UART1 TX=7/RX=6/CTS=5/RTS=4, but many devkits run AT on UART0 GPIO20/21),
  XTAL_32K_P/N (GPIO0/GPIO1, busy when `CONFIG_RTC_CLK_SRC_EXT_CRYS` /
  `CONFIG_BT_CTRL_LPCLK_SEL_EXT_32K_XTAL` are set, as in `module_esp32c3_default`),
  and C3 strapping pins 2/8/9.
- The `AT+GPIO=?` test handler prints a hardcoded pin list per target — update it in
  the same edit, and keep the summary string under 512 bytes.
