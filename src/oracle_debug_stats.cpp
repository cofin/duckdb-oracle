#include "oracle_debug_stats.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <mutex>

namespace duckdb {
namespace {

std::mutex &StatsLock() {
	static std::mutex lock;
	return lock;
}

OracleDebugStatsSnapshot &MutableStats() {
	static OracleDebugStatsSnapshot stats;
	return stats;
}

} // namespace

void OracleDebugResetStats() {
	lock_guard<std::mutex> guard(StatsLock());
	MutableStats() = OracleDebugStatsSnapshot();
}

void OracleDebugRecordFetch(idx_t rows) {
	lock_guard<std::mutex> guard(StatsLock());
	auto &stats = MutableStats();
	stats.fetch_calls++;
	stats.max_rows_per_fetch = MaxValue<idx_t>(stats.max_rows_per_fetch, rows);
}

void OracleDebugRecordScanBufferBytes(idx_t bytes) {
	lock_guard<std::mutex> guard(StatsLock());
	auto &stats = MutableStats();
	stats.scan_buffer_bytes = MaxValue<idx_t>(stats.scan_buffer_bytes, bytes);
}

void OracleDebugRecordWriteExecute(idx_t rows) {
	lock_guard<std::mutex> guard(StatsLock());
	auto &stats = MutableStats();
	stats.write_execute_calls++;
	stats.max_write_iters = MaxValue<idx_t>(stats.max_write_iters, rows);
}

void OracleDebugRecordWriteBufferBytes(idx_t bytes) {
	lock_guard<std::mutex> guard(StatsLock());
	auto &stats = MutableStats();
	stats.write_buffer_bytes = MaxValue<idx_t>(stats.write_buffer_bytes, bytes);
}

void OracleDebugRecordQuery(const string &query) {
	lock_guard<std::mutex> guard(StatsLock());
	MutableStats().last_query = query;
}

OracleDebugStatsSnapshot OracleDebugGetStats() {
	lock_guard<std::mutex> guard(StatsLock());
	return MutableStats();
}

idx_t OracleDebugGetCounter(const string &name) {
	auto key = StringUtil::Lower(name);
	auto stats = OracleDebugGetStats();
	if (key == "fetch_calls") {
		return stats.fetch_calls;
	}
	if (key == "max_rows_per_fetch") {
		return stats.max_rows_per_fetch;
	}
	if (key == "scan_buffer_bytes") {
		return stats.scan_buffer_bytes;
	}
	if (key == "write_execute_calls") {
		return stats.write_execute_calls;
	}
	if (key == "max_write_iters") {
		return stats.max_write_iters;
	}
	if (key == "write_buffer_bytes") {
		return stats.write_buffer_bytes;
	}
	throw InvalidInputException("Unknown Oracle debug counter \"%s\"", name.c_str());
}

string OracleDebugGetLastQuery() {
	return OracleDebugGetStats().last_query;
}

} // namespace duckdb
