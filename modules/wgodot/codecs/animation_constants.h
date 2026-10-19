// wgodot-changes::file
#pragma once

#include "w_animation.h"

// File-local spelling only: equality here never implies shared mutable keys.
class WAnimationConstants {
	struct Candidate {
		String literal;
		uint64_t count = 0;
		uint32_t order = 0;
		uint32_t id = 0;
	};
	struct FrequentFirst {
		bool operator()(const Candidate *p_left, const Candidate *p_right) const {
			return p_left->count == p_right->count ? p_left->order < p_right->order : p_left->count > p_right->count;
		}
	};
	struct ByID {
		bool operator()(const Candidate *p_left, const Candidate *p_right) const { return p_left->id < p_right->id; }
	};
	HashMap<String, Candidate> candidates;
	LocalVector<const Candidate *> selected;

public:
	static String name(uint32_t p_id);
	static bool parse_id(const String &p_name, uint32_t &r_id);
	void count(const String &p_expression);
	void select(const Vector<WAnimation::CodecConstant> &p_previous);
	String encode(const String &p_expression) const;
	String declarations() const;
	Vector<WAnimation::CodecConstant> save_hints() const;
};
