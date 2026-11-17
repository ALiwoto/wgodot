// wgodot-changes::file
#include "curve_codec.h"

#include "curve_payload.h"
#include "resource_codec.h"

#include "scene/resources/curve.h"

namespace {
// Curve's internal storage is a repeated (Vector2, float, float, int, int)
// record, not an untyped application Array. Validate that schema in both paths.
Error read_points(Curve *p_curve, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error) {
	using VP = VariantParser;
	const auto expected = [&](VP::TokenType p_type) -> Error {
		RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
		if (p_token.type != p_type) {
			r_error = "Unexpected delimiter in Curve point.";
			return ERR_PARSE_ERROR;
		}
		return OK;
	};
	if (p_token.type != VP::TK_BRACKET_OPEN) {
		r_error = "Curve._data requires an array of point records.";
		return ERR_PARSE_ERROR;
	}
	LocalVector<Curve::Point> points;
	while (true) {
		RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
		if (p_token.type == VP::TK_BRACKET_CLOSE) {
			break;
		}
		Curve::Point point;
		if (p_token.type == VP::TK_PARENTHESIS_OPEN) {
			for (int component = 0; component < 2; component++) {
				RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
				if (p_token.type != VP::TK_NUMBER) {
					r_error = "Curve position requires two numeric components: (x, y).";
					return ERR_PARSE_ERROR;
				}
				point.position[component] = p_token.value;
				RETURN_IF_ERROR(expected(component == 0 ? VP::TK_COMMA : VP::TK_PARENTHESIS_CLOSE));
			}
		} else if (p_token.type == VP::TK_IDENTIFIER && String(p_token.value) == "Vector2") {
			Variant position;
			RETURN_IF_ERROR(VP::parse_value(p_token, position, p_stream, r_line, r_error));
			point.position = position;
		} else {
			r_error = "Curve point position requires (x, y) or Vector2(x, y).";
			return ERR_PARSE_ERROR;
		}
		for (int field = 0; field < 4; field++) {
			RETURN_IF_ERROR(expected(VP::TK_COMMA));
			RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
			if (p_token.type != VP::TK_NUMBER || (field >= 2 && p_token.value.get_type() != Variant::INT)) {
				r_error = "Curve tangents require numbers and tangent modes require integers.";
				return ERR_PARSE_ERROR;
			}
			if (field == 0) {
				point.left_tangent = p_token.value;
			} else if (field == 1) {
				point.right_tangent = p_token.value;
			} else {
				const int64_t mode = p_token.value;
				if (mode < 0 || mode >= Curve::TANGENT_MODE_COUNT) {
					r_error = "Curve tangent mode is outside its declared enum.";
					return ERR_PARSE_ERROR;
				}
				(field == 2 ? point.left_mode : point.right_mode) = Curve::TangentMode(mode);
			}
		}
		if (p_curve) {
			points.push_back(point);
		}
		RETURN_IF_ERROR(VP::get_token(p_stream, p_token, r_line, r_error));
		if (p_token.type == VP::TK_BRACKET_CLOSE) {
			break;
		}
		if (p_token.type != VP::TK_COMMA) {
			r_error = "Expected a comma or closing bracket after Curve point.";
			return ERR_PARSE_ERROR;
		}
	}
	if (p_curve) {
		p_curve->set_native_points(std::move(points));
	}
	return OK;
}

class CurveCodec : public WResourceCodec {
public:
	bool owns_property(const StringName &p_name) const override { return p_name == SNAME("_data"); }
	bool owns_native_property(const StringName &p_name) const override {
		return p_name == SNAME("_limits") || p_name == SNAME("_data") || p_name == SNAME("point_count");
	}
	bool requires_native_factory() const override { return true; }
	bool owns_assignment(const String &p_name) const override { return p_name == "_data"; }
	Error read(Resource *p_resource, VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *) const override {
		return read_points(Object::cast_to<Curve>(p_resource), p_token, p_stream, r_line, r_error);
	}
	Error check_types(VariantParser::Token &p_token, VariantParser::Stream *p_stream, int &r_line, String &r_error, VariantParser::ResourceParser *) const override {
		return read_points(nullptr, p_token, p_stream, r_line, r_error);
	}
	Error write(const Resource *p_resource, String &r_text, VariantWriter::EncodeResourceFunc, void *) const override {
		const Curve *curve = Object::cast_to<Curve>(p_resource);
		ERR_FAIL_NULL_V(curve, ERR_INVALID_DATA);
		Vector<String> points;
		for (int i = 0; i < curve->get_point_count(); i++) {
			const Curve::Point point = curve->get_point(i);
			String position;
			String left;
			String right;
			RETURN_IF_ERROR(VariantWriter::write_to_string(point.position, position));
			RETURN_IF_ERROR(VariantWriter::write_to_string(point.left_tangent, left));
			RETURN_IF_ERROR(VariantWriter::write_to_string(point.right_tangent, right));
			points.push_back(position.trim_prefix("Vector2") + ", " + left + ", " + right + ", " + itos(point.left_mode) + ", " + itos(point.right_mode));
		}
		r_text = "_data = [" + String(", ").join(points) + "]\n";
		return OK;
	}
	Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const override {
		const Curve *curve = Object::cast_to<Curve>(p_resource);
		ERR_FAIL_NULL_V(curve, ERR_INVALID_DATA);
		p_writer.include("modules/wgodot/codecs/curve_payload.h");
		const String minimum = p_writer.value(curve->get_min_value());
		const String maximum = p_writer.value(curve->get_max_value());
		const String domain_minimum = p_writer.value(curve->get_min_domain());
		const String domain_maximum = p_writer.value(curve->get_max_domain());
		p_writer.line(p_target + "->set_native_limits(" + minimum + ", " + maximum + ", " + domain_minimum + ", " + domain_maximum + ");");
		const String points = p_writer.payload("LocalVector<Curve::Point>", WGodotNativePayload::CURVE_POINTS, [&](FileAccess &file) {
			file.store_64(curve->get_point_count());
			for (int i = 0; i < curve->get_point_count(); i++) {
				WGodotNativePayload::Value<Curve::Point>::write(file, curve->get_point(i));
			}
		});
		p_writer.line(p_target + "->set_native_points(" + points + ");");
		return OK;
	}
};

CurveCodec curve_codec;
} // namespace

void register_curve_codec() {
	WResourceCodec::register_codec("Curve", &curve_codec);
}

void unregister_curve_codec() {
	WResourceCodec::unregister_codec("Curve");
}
