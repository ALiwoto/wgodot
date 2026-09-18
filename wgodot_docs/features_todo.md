# WGodot Feature TODO


## null-safety

Add strict null checking option and mechanisms, to prevent crashes in gd2cpp.

## Optimize gd2cpp

Optimize gd2cpp as much as possible, e.g. optimize the cpp codegen or add better cpp type alternatives etc.

## Diagnostic redaction for gd2cpp

basically remove print, push_error, push_warning etc stuff that leaks the "game logic" in the code, and instead add vague terms such as ERZ_123 (with IDs stored in a manifest file so runtime logs can be recovered).
