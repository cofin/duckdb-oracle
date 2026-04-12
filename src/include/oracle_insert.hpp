#pragma once

#include "duckdb/execution/physical_operator.hpp"
#include "oracle_write.hpp"

namespace duckdb {

class PhysicalOracleInsert : public PhysicalOperator {
public:
	PhysicalOracleInsert(PhysicalPlan &physical_plan, vector<LogicalType> types, string table_name,
	                     string connection_string, vector<string> column_names, vector<LogicalType> column_types,
	                     idx_t estimated_cardinality);

	// Sink interface
	unique_ptr<GlobalSinkState> GetGlobalSinkState(ClientContext &context) const override;
	unique_ptr<LocalSinkState> GetLocalSinkState(ExecutionContext &context) const override;
	SinkResultType Sink(ExecutionContext &context, DataChunk &chunk, OperatorSinkInput &input) const override;
	SinkCombineResultType Combine(ExecutionContext &context, OperatorSinkCombineInput &input) const override;
	SinkFinalizeType Finalize(Pipeline &pipeline, Event &event, ClientContext &context,
	                          OperatorSinkFinalizeInput &input) const override;

	bool IsSink() const override {
		return true;
	}
	bool ParallelSink() const override {
		return false;
	}

	// Source interface (returns row count)
	SourceResultType GetDataInternal(ExecutionContext &context, DataChunk &chunk,
	                                 OperatorSourceInput &input) const override;
	bool IsSource() const override {
		return true;
	}

private:
	string table_name;
	string connection_string;
	vector<string> column_names;
	vector<LogicalType> column_types;
};

} // namespace duckdb
