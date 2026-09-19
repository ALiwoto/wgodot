# Removed features

The features in this file has been removed and are no longer available in the engine fork. Using them may result in compiler error.


## @obfuscate

Replaced with gd2cpp.

### Script path obfuscation

Option: `wgodot/export/obfuscate_file_paths` and `@Obfuscate_path`.
Removed due to ineffectiveness and replaced by gd2cpp.

### GDScript Name obfuscation

Replaced with gd2cpp.

### Obfuscation strategy

Option: `wgodot/export/obfuscation_strategy`.

Replaced with gd2cpp.

### Built-in/native name aliasing

Due to its ineffectiveness and being replaced by gd2cpp, it has been removed.

## Dead-code injection

Option: `wgodot/export/dead_code_injection_enabled`.

Due to being inefficient and increasing runtime costs. If you have been using this feature, you should consider migrating to [gd2cpp](./gd2cpp.md). C++ compilers will mainly remove deadcode anyway.

