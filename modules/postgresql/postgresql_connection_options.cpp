// wgodot-changes::file
#include "postgresql_connection_options.h"

void PostgreSQLConnectionOptions::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_address", "value"), &PostgreSQLConnectionOptions::set_address);
	ClassDB::bind_method(D_METHOD("get_address"), &PostgreSQLConnectionOptions::get_address);
	ClassDB::bind_method(D_METHOD("set_port", "value"), &PostgreSQLConnectionOptions::set_port);
	ClassDB::bind_method(D_METHOD("get_port"), &PostgreSQLConnectionOptions::get_port);
	ClassDB::bind_method(D_METHOD("set_database", "value"), &PostgreSQLConnectionOptions::set_database);
	ClassDB::bind_method(D_METHOD("get_database"), &PostgreSQLConnectionOptions::get_database);
	ClassDB::bind_method(D_METHOD("set_username", "value"), &PostgreSQLConnectionOptions::set_username);
	ClassDB::bind_method(D_METHOD("get_username"), &PostgreSQLConnectionOptions::get_username);
	ClassDB::bind_method(D_METHOD("set_password", "value"), &PostgreSQLConnectionOptions::set_password);
	ClassDB::bind_method(D_METHOD("get_password"), &PostgreSQLConnectionOptions::get_password);
	ClassDB::bind_method(D_METHOD("set_hostname", "value"), &PostgreSQLConnectionOptions::set_hostname);
	ClassDB::bind_method(D_METHOD("get_hostname"), &PostgreSQLConnectionOptions::get_hostname);
	ClassDB::bind_method(D_METHOD("set_root_certificate", "value"), &PostgreSQLConnectionOptions::set_root_certificate);
	ClassDB::bind_method(D_METHOD("get_root_certificate"), &PostgreSQLConnectionOptions::get_root_certificate);
	ClassDB::bind_method(D_METHOD("set_tls_mode", "value"), &PostgreSQLConnectionOptions::set_tls_mode);
	ClassDB::bind_method(D_METHOD("get_tls_mode"), &PostgreSQLConnectionOptions::get_tls_mode);
	BIND_ENUM_CONSTANT(TLS_UNKNOWN);
	BIND_ENUM_CONSTANT(TLS_DISABLED);
	BIND_ENUM_CONSTANT(TLS_VERIFY_FULL);
}
