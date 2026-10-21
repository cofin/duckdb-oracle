#include "oracle_insert.hpp"
#include "oracle_connection_manager.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <atomic>

namespace duckdb {

struct OracleInsertGlobalState : public GlobalSinkState {
	OracleInsertGlobalState(unique_ptr<OracleWriteBindData> bind_data_p, unique_ptr<OracleWriteGlobalState> gstate_p)
	    : bind_data(std::move(bind_data_p)), gstate(std::move(gstate_p)), insert_count(0) {
	}

	unique_ptr<OracleWriteBindData> bind_data;
	unique_ptr<OracleWriteGlobalState> gstate;
	std::atomic<idx_t> insert_count {0};
};

struct OracleInsertLocalState : public LocalSinkState {};

PhysicalOracleInsert::PhysicalOracleInsert(PhysicalPlan &physical_plan, vector<LogicalType> types, string schema_name_p,
                                           string table_name_p, string connection_string_p, string wallet_path_p,
                                           OracleSettings settings_p, vector<string> column_names_p,
                                           vector<LogicalType> column_types_p, idx_t estimated_cardinality)
    : PhysicalOperator(physical_plan, PhysicalOperatorType::EXTENSION, std::move(types), estimated_cardinality),
      schema_name(std::move(schema_name_p)), table_name(std::move(table_name_p)),
      connection_string(std::move(connection_string_p)), wallet_path(std::move(wallet_path_p)),
      settings(std::move(settings_p)), column_names(std::move(column_names_p)),
      column_types(std::move(column_types_p)) {
}

unique_ptr<GlobalSinkState> PhysicalOracleInsert::GetGlobalSinkState(ClientContext &context) const {
	auto bind_data = make_uniq<OracleWriteBindData>();
	bind_data->schema_name = schema_name;
	bind_data->object_name = table_name;
	bind_data->connection_string = connection_string;
	bind_data->wallet_path = wallet_path;
	bind_data->settings = settings;
	bind_data->column_names = column_names;
	bind_data->column_types = column_types;

	PrepareOracleWriteBindData(*bind_data);

	auto gstate = OracleWriteInitGlobal(context, *bind_data);

	return make_uniq<OracleInsertGlobalState>(std::move(bind_data), std::move(gstate));
}

unique_ptr<LocalSinkState> PhysicalOracleInsert::GetLocalSinkState(ExecutionContext &context) const {
	(void)context;
	return make_uniq<OracleInsertLocalState>();
}

SinkResultType PhysicalOracleInsert::Sink(ExecutionContext &context, DataChunk &chunk, OperatorSinkInput &input) const {
	auto &gstate = input.global_state.Cast<OracleInsertGlobalState>();

	OracleWriteSink(context, *gstate.bind_data, *gstate.gstate, chunk);
	gstate.insert_count.fetch_add(chunk.size(), std::memory_order_relaxed);

	return SinkResultType::NEED_MORE_INPUT;
}

SinkCombineResultType PhysicalOracleInsert::Combine(ExecutionContext &context, OperatorSinkCombineInput &input) const {
	(void)context;
	(void)input;
	return SinkCombineResultType::FINISHED;
}

SinkFinalizeType PhysicalOracleInsert::Finalize(Pipeline &pipeline, Event &event, ClientContext &context,
                                                OperatorSinkFinalizeInput &input) const {
	(void)pipeline;
	(void)event;
	(void)context;
	auto &gstate = input.global_state.Cast<OracleInsertGlobalState>();

	OracleWriteFinalize(*gstate.gstate);

	return SinkFinalizeType::READY;
}

SourceResultType PhysicalOracleInsert::GetDataInternal(ExecutionContext &context, DataChunk &chunk,
                                                       OperatorSourceInput &input) const {
	(void)context;
	(void)input;
	auto &gstate = sink_state->Cast<OracleInsertGlobalState>();
	FlatVector::GetDataMutable<int64_t>(chunk.data[0])[0] =
	    NumericCast<int64_t>(gstate.insert_count.load(std::memory_order_relaxed));
	chunk.SetChildCardinality(1);
	return SourceResultType::FINISHED;
}

} // namespace duckdb
