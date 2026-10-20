// wgodot-changes::file
#include "curve_codec.h"

#include "native_literals.h"
#include "resource_codec.h"
#include "scene/resources/curve.h"

namespace {
class CurveCodec : public WResourceCodec {
public:
	bool owns_property(const StringName &) const override { return false; }
	bool owns_native_property(const StringName &p_name) const override {
		return p_name == SNAME("_limits") || p_name == SNAME("_data") || p_name == SNAME("point_count");
	}
	bool requires_native_factory() const override { return true; }
	bool owns_assignment(const String &) const override { return false; }
	Error read(Resource *, VariantParser::Token &, VariantParser::Stream *, int &, String &, VariantParser::ResourceParser *) const override {
		return ERR_UNAVAILABLE;
	}
	Error write(const Resource *, String &r_text, VariantWriter::EncodeResourceFunc, void *) const override {
		r_text = String();
		return OK;
	}
	Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const override {
		const Curve *curve = Object::cast_to<Curve>(p_resource);
		ERR_FAIL_NULL_V(curve, ERR_INVALID_DATA);
		using WGodotCppLiterals::number;
		p_writer.include("scene/resources/curve.h");
		p_writer.line(p_target + "->set_native_limits(" + number(curve->get_min_value()) + ", " + number(curve->get_max_value()) + ", " + number(curve->get_min_domain()) + ", " + number(curve->get_max_domain()) + ");");
		if (curve->get_point_count() == 0) {
			return OK;
		}
		const String points = p_writer.symbol();
		p_writer.line("LocalVector<Curve::Point> " + points + ";");
		p_writer.line(points + ".resize(" + itos(curve->get_point_count()) + ");");
		for (int i = 0; i < curve->get_point_count(); i++) {
			const Curve::Point point = curve->get_point(i);
			p_writer.line(points + "[" + itos(i) + "] = Curve::Point(Vector2(" + number(point.position.x) + ", " + number(point.position.y) + "), " + number(point.left_tangent) + ", " + number(point.right_tangent) + ", Curve::TangentMode(" + itos(point.left_mode) + "), Curve::TangentMode(" + itos(point.right_mode) + "));");
		}
		p_writer.line(p_target + "->set_native_points(std::move(" + points + "));");
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
