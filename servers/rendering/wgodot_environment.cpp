// wgodot-changes::file
#include "renderer_scene_render.h"
#include "storage/environment_storage.h"

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
