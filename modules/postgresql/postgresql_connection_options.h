// wgodot-changes::file
#pragma once

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"

class PostgreSQLConnectionOptions : public RefCounted {
	GDCLASS(PostgreSQLConnectionOptions, RefCounted);

public:
	enum TLSMode {
		TLS_UNKNOWN,
		TLS_DISABLED,
		TLS_VERIFY_FULL,
	};

private:
	String address = "127.0.0.1";
	int port = 5432;
	String database;
	String username;
	String password;
	String hostname;
	String root_certificate;
	TLSMode tls_mode = TLS_VERIFY_FULL;

protected:
	static void _bind_methods();

public:
	void set_address(const String &p_value) { address = p_value; }
	String get_address() const { return address; }
	void set_port(int p_value) { port = p_value; }
	int get_port() const { return port; }
	void set_database(const String &p_value) { database = p_value; }
	String get_database() const { return database; }
	void set_username(const String &p_value) { username = p_value; }
	String get_username() const { return username; }
	void set_password(const String &p_value) { password = p_value; }
	String get_password() const { return password; }
	void set_hostname(const String &p_value) { hostname = p_value; }
	String get_hostname() const { return hostname; }
	void set_root_certificate(const String &p_value) { root_certificate = p_value; }
	String get_root_certificate() const { return root_certificate; }
	void set_tls_mode(TLSMode p_value) { tls_mode = p_value; }
	TLSMode get_tls_mode() const { return tls_mode; }
};

VARIANT_ENUM_CAST(PostgreSQLConnectionOptions::TLSMode);
