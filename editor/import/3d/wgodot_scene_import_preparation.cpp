// wgodot-changes::file

#include "resource_importer_scene.h"

Ref<EditorSceneFormatImporter> ResourceImporterScene::_get_format_importer(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	for (const Ref<EditorSceneFormatImporter> &importer : scene_importers) {
		List<String> extensions;
		importer->get_extensions(&extensions);
		for (const String &candidate : extensions) {
			if (candidate.to_lower() == extension) {
				return importer;
			}
		}
	}
	return Ref<EditorSceneFormatImporter>();
}

int ResourceImporterScene::_get_import_flags(const HashMap<StringName, Variant> &p_options) const {
	int flags = 0;
	if (_scene_import_type == "AnimationLibrary") {
		flags |= EditorSceneFormatImporter::IMPORT_ANIMATION;
		flags |= EditorSceneFormatImporter::IMPORT_DISCARD_MESHES_AND_MATERIALS;
	} else if (bool(p_options["animation/import"])) {
		flags |= EditorSceneFormatImporter::IMPORT_ANIMATION;
	}
	if (bool(p_options["skins/use_named_skins"])) {
		flags |= EditorSceneFormatImporter::IMPORT_USE_NAMED_SKIN_BINDS;
	}
	if (bool(p_options["meshes/ensure_tangents"])) {
		flags |= EditorSceneFormatImporter::IMPORT_GENERATE_TANGENT_ARRAYS;
	}
	if (bool(p_options["meshes/force_disable_compression"])) {
		flags |= EditorSceneFormatImporter::IMPORT_FORCE_DISABLE_MESH_COMPRESSION;
	}
	return flags;
}

void ResourceImporterScene::prepare_import(const String &p_source_file, const HashMap<StringName, Variant> &p_options) {
	Ref<EditorSceneFormatImporter> importer = _get_format_importer(p_source_file);
	if (importer.is_valid()) {
		importer->prepare_import_scene(p_source_file, _get_import_flags(p_options), p_options);
	}
}

void ResourceImporterScene::finish_prepared_imports() {
	for (const Ref<EditorSceneFormatImporter> &importer : scene_importers) {
		importer->finish_prepared_scenes();
	}
}
