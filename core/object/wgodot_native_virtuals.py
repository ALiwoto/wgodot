# wgodot-changes::file

# Reuse Godot's existing callback argument/return conversion for native game callbacks.
# Ordinary editor/template builds retain the upstream macro expansion.
native_call = """\\
        if (!_get_extension()) {\\
            if (unlikely(!_gdvirtual_##$VARNAME)) {\\
                _gdvirtual_##$VARNAME = reinterpret_cast<void *>(_wgodot_get_native_virtual(_gdvirtual_##$VARNAME##_sn));\\
                if (!_gdvirtual_##$VARNAME) {\\
                    _gdvirtual_##$VARNAME = reinterpret_cast<void *>(_INVALID_GDVIRTUAL_FUNC_ADDR);\\
                }\\
            }\\
            if (_gdvirtual_##$VARNAME != reinterpret_cast<void *>(_INVALID_GDVIRTUAL_FUNC_ADDR)) {\\
			$CALLSIARGS\\
                $CALLSIBEGIN reinterpret_cast<WGodotNativeVirtual>(_gdvirtual_##$VARNAME)(const_cast<Object *>(static_cast<const Object *>(this)), $CALLSIARGPASS);\\
				$CALLSIRET\\
                return true;\\
            }\\
        }"""

native_has_method = """\\
        if (!_get_extension() && _wgodot_get_native_virtual(_gdvirtual_##$VARNAME##_sn)) {\\
            return true;\\
        }"""


def add_native_variant(prototype, compatibility):
    if compatibility:
        return prototype
    native = prototype.replace("$SCRIPTCALL", "$SCRIPTCALL" + native_call)
    native = native.replace("$SCRIPTHASMETHOD", "$SCRIPTHASMETHOD" + native_has_method)
    return "#ifdef WGODOT_NATIVE_GAME\n" + native + "#else\n" + prototype + "#endif\n\n"
