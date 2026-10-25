// wgodot-changes::file

#ifndef USE_1D_LUT
// LUT entries contain display-encoded RGB at inclusive grid endpoints.
// Parameters: log2 scale, log2 bias, positive linear offset, output gain.
vec3 tonemap_log_lut(vec3 color) {
	vec3 coordinates = clamp(log2(max(color, vec3(0.0)) + params.tonemapper_params.z)
			* params.tonemapper_params.x + params.tonemapper_params.y, vec3(0.0), vec3(1.0));
	vec3 size = vec3(textureSize(source_color_correction, 0));
	coordinates = (coordinates * (size - 1.0) + 0.5) / size;
	return textureLod(source_color_correction, coordinates, 0.0).rgb * params.tonemapper_params.w;
}
#endif
