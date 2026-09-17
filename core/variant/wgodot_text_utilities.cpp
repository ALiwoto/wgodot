// wgodot-changes::file
#include "variant_utility.h"

#include "core/os/os.h"

String VariantUtilityFunctions::str(const WGodotText::Arguments &p_args) {
	return p_args.join();
}

void VariantUtilityFunctions::print(const WGodotText::Arguments &p_args) {
	print_line(p_args.join());
}

void VariantUtilityFunctions::print_rich(const WGodotText::Arguments &p_args) {
	print_line_rich(p_args.join());
}

void VariantUtilityFunctions::_print_verbose(const WGodotText::Arguments &p_args) {
	if (OS::get_singleton()->is_stdout_verbose()) {
		print_line(p_args.join());
	}
}

void VariantUtilityFunctions::printerr(const WGodotText::Arguments &p_args) {
	print_error(p_args.join());
}

void VariantUtilityFunctions::printt(const WGodotText::Arguments &p_args) {
	print_line(p_args.join("\t"));
}

void VariantUtilityFunctions::prints(const WGodotText::Arguments &p_args) {
	print_line(p_args.join(" "));
}

void VariantUtilityFunctions::printraw(const WGodotText::Arguments &p_args) {
	print_raw(p_args.join());
}

void VariantUtilityFunctions::push_error(const WGodotText::Arguments &p_args) {
	ERR_PRINT(p_args.join());
}

void VariantUtilityFunctions::push_warning(const WGodotText::Arguments &p_args) {
	WARN_PRINT(p_args.join());
}
