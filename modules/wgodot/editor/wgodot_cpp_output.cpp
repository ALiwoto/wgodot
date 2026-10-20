// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_source.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

// Copy semantic inputs, never per-class lowering state or output accumulators.
// Each worker has private collectors; the prepared catalog is read-only until join.
WGodotCppEmitter::WGodotCppEmitter(const WGodotCppEmitter &p_parent) :
		project(p_parent.project), trace_enabled(p_parent.trace_enabled), signatures(p_parent.signatures),
		staging_directory(p_parent.staging_directory), prepared_resource_ids(&p_parent.resource_ids),
		native_headers(p_parent.native_headers), native_cpp_names(p_parent.native_cpp_names), native_methods(p_parent.native_methods),
		interface_property_getters(p_parent.interface_property_getters), interface_property_setters(p_parent.interface_property_setters),
		inherited_classes(p_parent.inherited_classes), preload_indices(p_parent.preload_indices), preloads(p_parent.preloads),
		container_constant_owners(p_parent.container_constant_owners), class_container_constants(p_parent.class_container_constants),
		class_lifecycles(p_parent.class_lifecycles) {
}

WGodotCppEmitter::~WGodotCppEmitter() {
	if (!owns_staging_directory) {
		return;
	}
	// Only this run's private, flat staging directory; never recurse into user output.
	for (const String &name : DirAccess::get_files_at(staging_directory)) {
		DirAccess::remove_absolute(staging_directory.path_join(name));
	}
	DirAccess::remove_absolute(staging_directory);
}

void WGodotCppEmitter::store_file(const String &p_name, const String &p_contents) {
	if (output_error != OK) {
		return;
	}
	const String path = staging_directory.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &output_error);
	if (file.is_valid()) {
		file->store_string(p_contents);
		file->flush();
		output_error = file->get_error();
	}
	if (output_error != OK) {
		diagnostics.push_back("Cannot write generated source " + path + ": " + error_names[output_error]);
		return;
	}
	files.insert(p_name, p_contents.sha256_text());
}

void WGodotCppEmitter::store_source(const String &p_name, const String &p_header, WGodotCppSource &p_declarations, const String &p_middle, WGodotCppSource &p_body, const String &p_footer) {
	if (output_error != OK) {
		return;
	}
	const String path = staging_directory.path_join(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &output_error);
	CryptoCore::SHA256Context digest;
	uint8_t hash[32];
	auto write = [&]() -> Error {
		RETURN_IF_ERROR(output_error);
		RETURN_IF_ERROR(digest.start());
		RETURN_IF_ERROR(WGodotCppSource::write_text(*file.ptr(), digest, p_header));
		RETURN_IF_ERROR(p_declarations.copy_to(*file.ptr(), digest));
		RETURN_IF_ERROR(WGodotCppSource::write_text(*file.ptr(), digest, p_middle));
		RETURN_IF_ERROR(p_body.copy_to(*file.ptr(), digest));
		RETURN_IF_ERROR(WGodotCppSource::write_text(*file.ptr(), digest, p_footer));
		file->flush();
		RETURN_IF_ERROR(file->get_error());
		return digest.finish(hash);
	};
	output_error = write();
	if (output_error != OK) {
		diagnostics.push_back("Cannot write generated source " + path + ": " + error_names[output_error]);
		return;
	}
	files.insert(p_name, String::hex_encode_buffer(hash, 32));
}

void WGodotCppEmitter::merge_resource_worker(WGodotCppEmitter &p_worker) {
	for (const auto &entry : p_worker.animation_property_paths) {
		animation_property_paths[entry.key] |= entry.value;
	}
	diagnostics.append_array(p_worker.diagnostics);
	if (output_error == OK) {
		output_error = p_worker.output_error;
	}
	for (const auto &file : p_worker.files) {
		files.insert(file.key, file.value);
	}
	for (const auto &owner : p_worker.native_access_methods) {
		for (const auto &method : owner.value) {
			native_access_methods[owner.key].insert(method.key, method.value);
		}
	}
	for (const auto &name : p_worker.used_native_interfaces) {
		used_native_interfaces.insert(name);
	}
	for (const auto &name : p_worker.used_native_headers) {
		used_native_headers.insert(name);
	}
	for (const auto *required : p_worker.required_classes) {
		required_classes.insert(required);
	}
	for (const KeyValue<Variant, Variant> &entry : p_worker.compiled_resources) {
		compiled_resources[entry.key] = entry.value;
	}
	for (const KeyValue<Variant, Variant> &entry : p_worker.compiled_resource_aliases) {
		compiled_resource_aliases[entry.key] = entry.value;
	}
}
