// wgodot-changes::file
#include "postgresql_connection.h"

#include "core/os/os.h"

#include <libpq-fe.h>

#ifdef WINDOWS_ENABLED
#include <winsock2.h>
#else
#include <poll.h>
#endif

namespace {
int socket_ready(int p_socket, bool p_read) {
#ifdef WINDOWS_ENABLED
	fd_set sockets;
	FD_ZERO(&sockets);
	FD_SET(SOCKET(p_socket), &sockets);
	fd_set errors = sockets;
	timeval timeout = {};
	return select(0, p_read ? &sockets : nullptr, p_read ? nullptr : &sockets, &errors, &timeout);
#else
	pollfd socket = { p_socket, short(p_read ? POLLIN : POLLOUT), 0 };
	return ::poll(&socket, 1, 0);
#endif
}

void ignore_notice(void *, const char *) {
}
} // namespace

Error PostgreSQLConnection::fail(Error p_error, const String &p_message) {
	close();
	last_error = p_message;
	state = STATE_FAILED;
	return p_error;
}

Error PostgreSQLConnection::start_connection(const Ref<PostgreSQLConnectionOptions> &p_options, int p_timeout_ms) {
	ERR_FAIL_COND_V(p_options.is_null(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(connection != nullptr, ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(p_timeout_ms <= 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(!p_options->get_address().is_valid_ip_address(), ERR_INVALID_PARAMETER, "Use a numeric address. Resolve database DNS outside the simulation tick; hostname supplies the TLS identity.");
	ERR_FAIL_COND_V(p_options->get_port() < 1 || p_options->get_port() > 65535, ERR_INVALID_PARAMETER);
	const PostgreSQLConnectionOptions::TLSMode tls = p_options->get_tls_mode();
	ERR_FAIL_COND_V(tls != PostgreSQLConnectionOptions::TLS_DISABLED && tls != PostgreSQLConnectionOptions::TLS_VERIFY_FULL, ERR_INVALID_PARAMETER);

	const CharString address = p_options->get_address().utf8();
	const CharString port = itos(p_options->get_port()).utf8();
	const CharString database = p_options->get_database().utf8();
	const CharString username = p_options->get_username().utf8();
	const CharString password = p_options->get_password().utf8();
	const CharString hostname = (p_options->get_hostname().is_empty() ? p_options->get_address() : p_options->get_hostname()).utf8();
	const CharString root = p_options->get_root_certificate().utf8();
	const char *keys[] = { "hostaddr", "port", "dbname", "user", "password", "host", "sslmode", "sslrootcert", "client_encoding", "application_name", nullptr };
	const char *values[] = { address.get_data(), port.get_data(), database.get_data(), username.get_data(), password.get_data(), hostname.get_data(), tls == PostgreSQLConnectionOptions::TLS_DISABLED ? "disable" : "verify-full", root.get_data(), "UTF8", "WGodot", nullptr };
	last_error = String();
	connection = PQconnectStartParams(keys, values, 0);
	if (!connection) {
		return fail(ERR_OUT_OF_MEMORY, "libpq could not allocate a connection.");
	}
	PQsetNoticeProcessor(connection, ignore_notice, nullptr);
	if (PQstatus(connection) == CONNECTION_BAD) {
		return fail(ERR_CANT_CONNECT, String::utf8(PQerrorMessage(connection)));
	}
	connect_poll_state = PGRES_POLLING_WRITING;
	deadline_usec = OS::get_singleton()->get_ticks_usec() + uint64_t(p_timeout_ms) * 1000;
	state = STATE_CONNECTING;
	return OK;
}

Error PostgreSQLConnection::send_query(const String &p_sql, const Ref<PostgreSQLParameters> &p_parameters, int p_timeout_ms) {
	ERR_FAIL_COND_V(state != STATE_READY, ERR_BUSY);
	ERR_FAIL_COND_V(p_timeout_ms <= 0 || p_sql.is_empty(), ERR_INVALID_PARAMETER);
	const int count = p_parameters.is_valid() ? p_parameters->parameters.size() : 0;
	ERR_FAIL_COND_V(count > 65535, ERR_INVALID_PARAMETER);
	Vector<Oid> types;
	Vector<const char *> values;
	Vector<int> lengths;
	Vector<int> formats;
	types.resize(count);
	values.resize(count);
	lengths.resize(count);
	formats.resize(count);
	for (int i = 0; i < count; i++) {
		const PostgreSQLParameters::Parameter &parameter = p_parameters->parameters[i];
		types.write[i] = parameter.oid;
		// An empty binary value still needs a non-null pointer; nullptr means SQL NULL.
		values.write[i] = parameter.null ? nullptr : parameter.bytes.is_empty() ? ""
																				: reinterpret_cast<const char *>(parameter.bytes.ptr());
		lengths.write[i] = parameter.bytes.size();
		formats.write[i] = 1;
	}
	if (!PQsendQueryParams(connection, p_sql.utf8().get_data(), count, types.ptr(), values.ptr(), lengths.ptr(), formats.ptr(), 1)) {
		return fail(ERR_CONNECTION_ERROR, String::utf8(PQerrorMessage(connection)));
	}
	deadline_usec = OS::get_singleton()->get_ticks_usec() + uint64_t(p_timeout_ms) * 1000;
	state = STATE_BUSY;
	return OK;
}

Error PostgreSQLConnection::poll() {
	if (state != STATE_CONNECTING && state != STATE_BUSY) {
		return state == STATE_FAILED ? ERR_CONNECTION_ERROR : OK;
	}
	if (OS::get_singleton()->get_ticks_usec() >= deadline_usec) {
		return fail(ERR_TIMEOUT, state == STATE_CONNECTING ? "Database connection timed out." : "Database query timed out; its outcome may be unknown. Connection closed without retry.");
	}
	if (state == STATE_CONNECTING) {
		const int socket = PQsocket(connection);
		if (socket < 0) {
			return fail(ERR_CANT_CONNECT, String::utf8(PQerrorMessage(connection)));
		}
		const int ready = socket_ready(socket, connect_poll_state == PGRES_POLLING_READING);
		if (ready < 0) {
			return fail(ERR_CONNECTION_ERROR, "Cannot poll the PostgreSQL connection socket.");
		}
		if (!ready) {
			return OK;
		}
		connect_poll_state = PQconnectPoll(connection);
		if (connect_poll_state == PGRES_POLLING_FAILED) {
			return fail(ERR_CANT_CONNECT, String::utf8(PQerrorMessage(connection)));
		}
		if (connect_poll_state == PGRES_POLLING_OK) {
			if (PQsetnonblocking(connection, 1) != 0) {
				return fail(ERR_CONNECTION_ERROR, String::utf8(PQerrorMessage(connection)));
			}
			state = STATE_READY;
		}
		return OK;
	}
	const int flush = PQflush(connection);
	if (flush < 0 || !PQconsumeInput(connection)) {
		return fail(ERR_CONNECTION_ERROR, String::utf8(PQerrorMessage(connection)));
	}
	if (flush || PQisBusy(connection)) {
		return OK;
	}
	PGresult *result = PQgetResult(connection);
	if (!result) {
		state = STATE_READY;
		return OK;
	}
	const ExecStatusType status = PQresultStatus(result);
	if (status == PGRES_COPY_IN || status == PGRES_COPY_OUT || status == PGRES_COPY_BOTH) {
		PQclear(result);
		return fail(ERR_UNAVAILABLE, "COPY is not supported by this query API. Connection closed.");
	}
	pending_result.instantiate();
	pending_result->result = result;
	state = STATE_RESULT_READY;
	return OK;
}

Ref<PostgreSQLResult> PostgreSQLConnection::take_result() {
	ERR_FAIL_COND_V(state != STATE_RESULT_READY, Ref<PostgreSQLResult>());
	Ref<PostgreSQLResult> result = pending_result;
	pending_result.unref();
	// Drain PQgetResult to nullptr before accepting another command, even after SQL errors.
	state = STATE_BUSY;
	return result;
}

PostgreSQLConnection::TransactionState PostgreSQLConnection::get_transaction_state() const {
	if (!connection) {
		return TRANSACTION_UNKNOWN;
	}
	switch (PQtransactionStatus(connection)) {
		case PQTRANS_IDLE:
			return TRANSACTION_IDLE;
		case PQTRANS_ACTIVE:
			return TRANSACTION_ACTIVE;
		case PQTRANS_INTRANS:
			return TRANSACTION_OPEN;
		case PQTRANS_INERROR:
			return TRANSACTION_FAILED;
		default:
			return TRANSACTION_UNKNOWN;
	}
}

void PostgreSQLConnection::close() {
	pending_result.unref();
	if (connection) {
		PQfinish(connection);
		connection = nullptr;
	}
	state = STATE_DISCONNECTED;
	deadline_usec = 0;
	last_error = String();
}

PostgreSQLConnection::~PostgreSQLConnection() {
	close();
}

void PostgreSQLConnection::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start_connection", "options", "timeout_ms"), &PostgreSQLConnection::start_connection, DEFVAL(10000));
	ClassDB::bind_method(D_METHOD("send_query", "sql", "parameters", "timeout_ms"), &PostgreSQLConnection::send_query, DEFVAL(Ref<PostgreSQLParameters>()), DEFVAL(10000));
	ClassDB::bind_method(D_METHOD("poll"), &PostgreSQLConnection::poll);
	ClassDB::bind_method(D_METHOD("take_result"), &PostgreSQLConnection::take_result);
	ClassDB::bind_method(D_METHOD("get_state"), &PostgreSQLConnection::get_state);
	ClassDB::bind_method(D_METHOD("get_transaction_state"), &PostgreSQLConnection::get_transaction_state);
	ClassDB::bind_method(D_METHOD("get_last_error"), &PostgreSQLConnection::get_last_error);
	ClassDB::bind_method(D_METHOD("close"), &PostgreSQLConnection::close);
	BIND_ENUM_CONSTANT(STATE_DISCONNECTED);
	BIND_ENUM_CONSTANT(STATE_CONNECTING);
	BIND_ENUM_CONSTANT(STATE_READY);
	BIND_ENUM_CONSTANT(STATE_BUSY);
	BIND_ENUM_CONSTANT(STATE_RESULT_READY);
	BIND_ENUM_CONSTANT(STATE_FAILED);
	BIND_ENUM_CONSTANT(TRANSACTION_UNKNOWN);
	BIND_ENUM_CONSTANT(TRANSACTION_IDLE);
	BIND_ENUM_CONSTANT(TRANSACTION_ACTIVE);
	BIND_ENUM_CONSTANT(TRANSACTION_OPEN);
	BIND_ENUM_CONSTANT(TRANSACTION_FAILED);
}
