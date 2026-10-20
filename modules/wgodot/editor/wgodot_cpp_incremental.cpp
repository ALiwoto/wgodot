// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_cache.h"

#include "modules/gdscript/wgodot_gd/inline_constants.h"

namespace {
template <class T>
PackedStringArray names(const HashSet<T> &p_values) {
	PackedStringArray result;
	for (const auto &value : p_values) {
		result.push_back(String(value));
	}
	result.sort();
	return result;
}
} // namespace

Dictionary WGodotCppEmitter::capture_contribution(const WGodotCppProject::Class *p_class) const {
	Dictionary payload;
	Dictionary outputs;
	for (const auto &file : files) {
		outputs[file.key] = file.value;
	}
	payload["files"] = outputs;
	Dictionary methods;
	for (const auto &owner : native_access_methods) {
		Dictionary entries;
		for (const auto &method : owner.value) {
			entries[method.key] = method.value;
		}
		methods[owner.key] = entries;
	}
	payload["methods"] = methods;
	payload["interfaces"] = names(used_native_interfaces);
	payload["headers"] = names(used_native_headers);
	payload["defaults"] = names(reflection_default_classes);
	payload["resource_paths"] = names(unit_resource_paths);
	Dictionary animations;
	for (const auto &entry : animation_property_paths) {
		animations[entry.key] = int64_t(entry.value);
	}
	payload["animations"] = animations;
	PackedStringArray required;
	for (const auto *node : required_classes) {
		if (const auto *type = project.find_class(node)) {
			required.push_back(type->cpp_name);
		}
	}
	payload["required"] = required;
	Dictionary preload_data;
	for (const String &path : unit_preloads) {
		const auto &preload = preloads[preload_indices[path]];
		Array entry;
		entry.push_back(preload.type);
		entry.push_back(preload.asynchronous);
		preload_data[path] = entry;
	}
	payload["preloads"] = preload_data;
	payload["compiled"] = compiled_resources.duplicate();
	payload["aliases"] = compiled_resource_aliases.duplicate();
	if (p_class) {
		if (const ClassLifecycle *lifecycle = class_lifecycles.getptr(p_class->node)) {
			payload["class"] = p_class->cpp_name;
			payload["lifecycle"] = int(lifecycle->prepare) | (int(lifecycle->fields) << 1) | (int(lifecycle->constructor) << 2) |
					(int(lifecycle->virtual_initializer) << 3) | (int(lifecycle->notifications) << 4) | (int(lifecycle->tasks) << 5);
		}
	}
	return payload;
}

bool WGodotCppEmitter::restore_contribution(const Dictionary &p_payload) {
	const PackedStringArray required = p_payload["required"];
	for (const String &name : required) {
		if (!classes_by_name.has(name)) {
			return false;
		}
	}
	const String lifecycle_class = p_payload.get("class", "");
	if (!lifecycle_class.is_empty() && !classes_by_name.has(lifecycle_class)) {
		return false;
	}
	const Dictionary outputs = p_payload["files"];
	for (const KeyValue<Variant, Variant> &entry : outputs) {
		files.insert(entry.key, entry.value);
	}
	const Dictionary methods = p_payload["methods"];
	for (const KeyValue<Variant, Variant> &owner : methods) {
		const Dictionary entries = owner.value;
		for (const KeyValue<Variant, Variant> &method : entries) {
			native_access_methods[StringName(owner.key)].insert(method.key, method.value);
		}
	}
	for (const String &name : PackedStringArray(p_payload["interfaces"])) {
		used_native_interfaces.insert(name);
	}
	for (const String &name : PackedStringArray(p_payload["headers"])) {
		used_native_headers.insert(name);
	}
	for (const String &name : PackedStringArray(p_payload["defaults"])) {
		reflection_default_classes.insert(name);
	}
	const Dictionary animations = p_payload["animations"];
	for (const KeyValue<Variant, Variant> &entry : animations) {
		animation_property_paths[NodePath(entry.key)] |= uint64_t(int64_t(entry.value));
	}
	for (const String &name : required) {
		required_classes.insert(classes_by_name[name]->node);
	}
	for (const String &path : PackedStringArray(p_payload["resource_paths"])) {
		resource_id(path);
	}
	const Dictionary preload_data = p_payload["preloads"];
	for (const KeyValue<Variant, Variant> &entry : preload_data) {
		const String path = entry.key;
		const Array data = entry.value;
		if (const int *index = preload_indices.getptr(path)) {
			preloads.write[*index].asynchronous &= bool(data[1]);
		} else {
			preload_indices.insert(path, preloads.size());
			preloads.push_back({ path, data[0], data[1] });
		}
	}
	const Dictionary compiled = p_payload["compiled"];
	for (const KeyValue<Variant, Variant> &entry : compiled) {
		compiled_resources[entry.key] = entry.value;
	}
	const Dictionary aliases = p_payload["aliases"];
	for (const KeyValue<Variant, Variant> &entry : aliases) {
		compiled_resource_aliases[entry.key] = entry.value;
	}
	if (!lifecycle_class.is_empty()) {
		const int flags = p_payload["lifecycle"];
		class_lifecycles.insert(classes_by_name[lifecycle_class]->node, {
			bool(flags & 1), bool(flags & 2), bool(flags & 4), bool(flags & 8), bool(flags & 16), bool(flags & 32) });
	}
	return true;
}

void WGodotCppEmitter::clear_contribution() {
	files.clear();
	native_access_methods.clear();
	used_native_interfaces.clear();
	used_native_headers.clear();
	reflection_default_classes.clear();
	animation_property_paths.clear();
	required_classes.clear();
	unit_resource_paths.clear();
	unit_preloads.clear();
	compiled_resources.clear();
	compiled_resource_aliases.clear();
}

void WGodotCppEmitter::emit_class(const WGodotCppProject::Class &p_class) {
	if (!diagnostics.is_empty() || output_error != OK) {
		return;
	}
	if (files.has(p_class.cpp_name + ".h") || class_lifecycles.has(p_class.node)) {
		return;
	}
	if (!required_classes.has(p_class.node) && WGodotGDScriptInlineConstants::can_omit_class(p_class.node)) {
		return;
	}
	if (p_class.node->base_type.kind == GDScriptParser::DataType::CLASS) {
		if (const auto *parent = project.find_class(p_class.node->base_type.class_type)) {
			required_classes.insert(parent->node);
			emit_class(*parent);
		}
	}
	if (!diagnostics.is_empty() || output_error != OK) {
		return;
	}
	const String *signature = signature_fingerprints.getptr(p_class.script_path);
	String context = (signature ? *signature : String()) + ":" + itos(inherited_classes.has(p_class.node));
	if (const auto *constants = class_container_constants.getptr(p_class.node)) {
		for (const auto *constant : *constants) {
			context += ":" + itos(used_container_constants.has(constant));
		}
	}
	const String key = "class:" + p_class.cpp_name;
	if (cache) {
		const Dictionary reused = cache->reuse(key, context);
		if (!reused.is_empty() && restore_contribution(reused)) {
			++reused_classes;
			return;
		}
	}
	// Isolate every unit's contributions. Capturing differences from a cumulative
	// set would lose repeated contributions when an earlier unit is removed.
	WGodotCppEmitter &worker = *script_worker;
	worker.clear_contribution();
	worker.class_lifecycles = class_lifecycles;
	worker.preloads = preloads;
	worker.preload_indices = preload_indices;
	worker.resource_ids.clear();
	worker.resource_id_owners.clear();
	worker.prepared_resource_ids = nullptr;
	worker.required_classes.insert(p_class.node);
	worker.emit_class_uncached(p_class);
	const Dictionary payload = worker.capture_contribution(&p_class);
	if (!worker.diagnostics.is_empty() || worker.output_error != OK) {
		diagnostics.append_array(worker.diagnostics);
		output_error = worker.output_error;
		return;
	}
	restore_contribution(payload);
	if (cache) {
		PackedStringArray dependencies = payload["resource_paths"];
		dependencies.push_back(p_class.script_path);
		// Classes whose constants are folded into this unit may no longer have
		// a header include, but are still semantic dependencies of its parser.
		cache->record(key, context, dependencies, payload);
	}
}
