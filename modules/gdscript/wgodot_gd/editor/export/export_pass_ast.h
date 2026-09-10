// wgodot-changes::file

#pragma once

#include "export_pass.h"
#include "source_rewrite.h"

namespace WGodotGDScriptExportTransform {

class AnalyzedExportPass : public ExportTransformationBase {
protected:
	HashMap<String, Ref<GDScriptParserRef>> analyzed_scripts;
	void setup_rewrite(const ExportPassInput &p_input, ExportPassOutput &r_output, const String &p_path, RewriteContext &r_rewrite) const;
	void finish_rewrite(const String &p_path, const RewriteContext &p_rewrite, ExportPassOutput &r_output) const;

public:
	Error analyze(const ExportAnalysisInput &p_input, String &r_error) override;
};

class ConstantsPass : public AnalyzedExportPass {
public:
	const char *get_name() const override { return "constants"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export"), SNAME("diagnostics"), SNAME("dead_code") }; }
	bool is_enabled(const TransformOptions &p_options) const override { return p_options.deconst_exports; }
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

class NamesPass : public AnalyzedExportPass {
public:
	const char *get_name() const override { return "names"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export"), SNAME("dead_code"), SNAME("constants"), SNAME("builtin_aliases") }; }
	bool is_enabled(const TransformOptions &p_options) const override { return p_options.obfuscate_names; }
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

class BuiltinAliasesPass : public AnalyzedExportPass {
	HashSet<StringName> identifiers;

public:
	const char *get_name() const override { return "builtin_aliases"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export"), SNAME("dead_code"), SNAME("constants") }; }
	bool is_enabled(const TransformOptions &p_options) const override { return p_options.obfuscate_builtin_names; }
	Error analyze(const ExportAnalysisInput &p_input, String &r_error) override;
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

class PathsPass : public AnalyzedExportPass {
public:
	const char *get_name() const override { return "paths"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export") }; }
	bool is_enabled(const TransformOptions &p_options) const override { return p_options.obfuscate_file_paths; }
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

class StringsPass : public AnalyzedExportPass {
public:
	const char *get_name() const override { return "strings"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export"), SNAME("diagnostics"), SNAME("dead_code"), SNAME("constants"), SNAME("paths") }; }
	bool is_enabled(const TransformOptions &p_options) const override { return p_options.obfuscate_strings; }
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

class CleanupPass : public AnalyzedExportPass {
public:
	const char *get_name() const override { return "cleanup"; }
	Vector<StringName> get_predecessors() const override { return { SNAME("no_export"), SNAME("diagnostics"), SNAME("dead_code"), SNAME("constants"), SNAME("builtin_aliases"), SNAME("names"), SNAME("paths"), SNAME("strings") }; }
	bool is_enabled(const TransformOptions &p_options) const override { return true; }
	Error transform(const ExportPassInput &p_input, ExportPassOutput &r_output, String &r_error) override;
};

} // namespace WGodotGDScriptExportTransform
