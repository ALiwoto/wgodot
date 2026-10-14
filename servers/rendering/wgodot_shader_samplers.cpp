// wgodot-changes::file
#include "shader_language.h"

Error ShaderLanguage::_parse_sampler_state(WGodotSamplerState &r_state) {
	if (r_state.enabled) {
		_set_error("Duplicated sampler_state hint.");
		return ERR_PARSE_ERROR;
	}
	if (_get_token().type != TK_PARENTHESIS_OPEN) {
		_set_expected_error("(");
		return ERR_PARSE_ERROR;
	}
	WGodotSamplerState::Address addresses[3];
	for (int axis = 0; axis < 3; axis++) {
		Token token = _get_token();
		if (token.type != TK_IDENTIFIER) {
			_set_error("Sampler address must be repeat, mirror or clamp.");
			return ERR_PARSE_ERROR;
		}
		if (token.text == "repeat") {
			addresses[axis] = WGodotSamplerState::REPEAT;
		} else if (token.text == "mirror") {
			addresses[axis] = WGodotSamplerState::MIRROR;
		} else if (token.text == "clamp") {
			addresses[axis] = WGodotSamplerState::CLAMP;
		} else {
			_set_error("Sampler address must be repeat, mirror or clamp.");
			return ERR_PARSE_ERROR;
		}
		if (_get_token().type != TK_COMMA) {
			_set_expected_error(",");
			return ERR_PARSE_ERROR;
		}
	}
	Token anisotropy = _get_token();
	if (!anisotropy.is_integer_constant() || anisotropy.constant < 1 || anisotropy.constant > 16 ||
			(uint32_t(anisotropy.constant) & (uint32_t(anisotropy.constant) - 1)) != 0) {
		_set_error("Sampler anisotropy must be 1, 2, 4, 8 or 16.");
		return ERR_PARSE_ERROR;
	}
	if (_get_token().type != TK_PARENTHESIS_CLOSE) {
		_set_expected_error(")");
		return ERR_PARSE_ERROR;
	}
	r_state.enabled = true;
	r_state.address_u = addresses[0];
	r_state.address_v = addresses[1];
	r_state.address_w = addresses[2];
	r_state.anisotropy = uint32_t(anisotropy.constant);
	return OK;
}
