// wgodot-changes::file
#include "core/object/class_db.h"
#include "scene/resources/environment.h"
#include "servers/rendering/renderer_scene_render.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/storage/environment_storage.h"

void Environment::_bind_geometry_fade_methods() {
	ClassDB::bind_method(D_METHOD("set_geometry_fade_color", "color"), &Environment::set_geometry_fade_color);
	ClassDB::bind_method(D_METHOD("get_geometry_fade_color"), &Environment::get_geometry_fade_color);
	ClassDB::bind_method(D_METHOD("set_geometry_fade_excluded_layers", "layers"), &Environment::set_geometry_fade_excluded_layers);
	ClassDB::bind_method(D_METHOD("get_geometry_fade_excluded_layers"), &Environment::get_geometry_fade_excluded_layers);
	ADD_GROUP("Geometry Fade", "geometry_fade_");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "geometry_fade_color"), "set_geometry_fade_color", "get_geometry_fade_color");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "geometry_fade_excluded_layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_geometry_fade_excluded_layers", "get_geometry_fade_excluded_layers");
}

void Environment::set_geometry_fade_color(const Color &p_color) {
	if (geometry_fade_color == p_color) {
		return;
	}
	geometry_fade_color = p_color;
	RS::get_singleton()->environment_set_geometry_fade(environment, geometry_fade_color, geometry_fade_excluded_layers);
}

void Environment::set_geometry_fade_excluded_layers(uint32_t p_layers) {
	if (geometry_fade_excluded_layers == p_layers) {
		return;
	}
	geometry_fade_excluded_layers = p_layers;
	RS::get_singleton()->environment_set_geometry_fade(environment, geometry_fade_color, geometry_fade_excluded_layers);
}

void RendererEnvironmentStorage::environment_set_geometry_fade(RID p_env, const Color &p_color, uint32_t p_excluded_layers) {
	Environment *env = environment_owner.get_or_null(p_env);
	ERR_FAIL_NULL(env);
	env->geometry_fade_color = p_color;
	env->geometry_fade_excluded_layers = p_excluded_layers;
}

Color RendererEnvironmentStorage::environment_get_geometry_fade_color(RID p_env) const {
	const Environment *env = environment_owner.get_or_null(p_env);
	ERR_FAIL_NULL_V(env, Color(0, 0, 0, 0));
	return env->geometry_fade_color;
}

uint32_t RendererEnvironmentStorage::environment_get_geometry_fade_excluded_layers(RID p_env) const {
	const Environment *env = environment_owner.get_or_null(p_env);
	ERR_FAIL_NULL_V(env, 0);
	return env->geometry_fade_excluded_layers;
}

void RendererSceneRender::environment_set_geometry_fade(RID p_env, const Color &p_color, uint32_t p_excluded_layers) {
	environment_storage.environment_set_geometry_fade(p_env, p_color, p_excluded_layers);
}

Color RendererSceneRender::environment_get_geometry_fade_color(RID p_env) const {
	return environment_storage.environment_get_geometry_fade_color(p_env);
}

uint32_t RendererSceneRender::environment_get_geometry_fade_excluded_layers(RID p_env) const {
	return environment_storage.environment_get_geometry_fade_excluded_layers(p_env);
}
