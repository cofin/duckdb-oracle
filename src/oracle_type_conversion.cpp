#include "oracle_type_conversion.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <cmath>

namespace duckdb {

static string ReadOracleString(const char *data, ub2 length) {
	if (!data || length == 0) {
		return string();
	}
	size_t actual_len = (data[length - 1] == '\0') ? static_cast<size_t>(length - 1) : static_cast<size_t>(length);
	return string(data, actual_len);
}

static string ConversionPrefix(const OracleConversionContext &context) {
	auto row_str = std::to_string(context.row_index);
	return StringUtil::Format("Failed to convert Oracle value for column \"%s\" (Oracle %s -> DuckDB %s, row %s)",
	                          context.column_name.c_str(), context.oracle_type.c_str(), context.duckdb_type.ToString(),
	                          row_str.c_str());
}

static string DisplayValue(const string &value) {
	if (value.size() <= 160) {
		return value;
	}
	return value.substr(0, 160) + "...";
}

static void ThrowConversionError(const OracleConversionContext &context, const string &value, const string &reason) {
	throw InvalidInputException("%s: %s. Value: \"%s\"", ConversionPrefix(context).c_str(), reason.c_str(),
	                            DisplayValue(value).c_str());
}

static string TrimmedText(const char *data, ub2 length) {
	auto value = ReadOracleString(data, length);
	StringUtil::Trim(value);
	return value;
}

bool TryParseOracleMetadataInt(const string &raw, int32_t &out) {
	auto trimmed = raw;
	StringUtil::Trim(trimmed);
	if (trimmed.empty()) {
		return false;
	}
	return TryCast::Operation<string_t, int32_t>(string_t(trimmed.data(), trimmed.size()), out);
}

bool TryParseOracleMetadataIndex(const string &raw, idx_t &out) {
	auto trimmed = raw;
	StringUtil::Trim(trimmed);
	if (trimmed.empty() || trimmed.front() == '-') {
		return false;
	}
	uint64_t parsed = 0;
	if (!TryCast::Operation<string_t, uint64_t>(string_t(trimmed.data(), trimmed.size()), parsed)) {
		return false;
	}
	out = static_cast<idx_t>(parsed);
	return true;
}

static int64_t ParseBigInt(const string &value, const OracleConversionContext &context) {
	int64_t result = 0;
	if (value.empty() || !TryCast::Operation<string_t, int64_t>(string_t(value.data(), value.size()), result)) {
		ThrowConversionError(context, value, "expected BIGINT literal");
	}
	return result;
}

static float ParseFloat(const string &value, const OracleConversionContext &context) {
	double dval = 0;
	if (value.empty() || !TryCast::Operation<string_t, double>(string_t(value.data(), value.size()), dval)) {
		ThrowConversionError(context, value, "expected FLOAT literal");
	}
	float result = static_cast<float>(dval);
	if (std::isfinite(dval) && !std::isfinite(result)) {
		ThrowConversionError(context, value, "FLOAT literal out of range");
	}
	return result;
}

static double ParseDouble(const string &value, const OracleConversionContext &context) {
	double result = 0;
	if (value.empty() || !TryCast::Operation<string_t, double>(string_t(value.data(), value.size()), result)) {
		ThrowConversionError(context, value, "expected DOUBLE literal");
	}
	return result;
}

static timestamp_t ParseTimestamp(const string &value, const OracleConversionContext &context) {
	if (value.empty()) {
		ThrowConversionError(context, value, "expected TIMESTAMP literal");
	}
	timestamp_t result;
	if (Timestamp::TryConvertTimestamp(value.c_str(), value.size(), result, true) == TimestampCastResult::SUCCESS) {
		return result;
	}
	try {
		return Timestamp::FromString(value, true);
	} catch (const std::exception &ex) {
		ThrowConversionError(context, value, "expected TIMESTAMP literal: " + string(ex.what()));
	}
	throw InternalException("unreachable TIMESTAMP conversion path");
}

static Value ParseDecimal(const string &value, const LogicalType &target_type, const OracleConversionContext &context) {
	try {
		return Value(value).DefaultCastAs(target_type);
	} catch (const std::exception &ex) {
		ThrowConversionError(context, value, "expected DECIMAL literal: " + string(ex.what()));
	}
	throw InternalException("unreachable DECIMAL conversion path");
}

void ParseOracleVectorElements(const string &text, const LogicalType &element_type, idx_t expected_dims,
                               vector<Value> &elements) {
	elements.clear();
	auto value = text;
	StringUtil::Trim(value);
	if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
		throw InvalidInputException("expected VECTOR_SERIALIZE JSON array");
	}

	value = value.substr(1, value.size() - 2);
	StringUtil::Trim(value);
	if (value.empty()) {
		if (expected_dims > 0) {
			auto expected_str = std::to_string(expected_dims);
			throw InvalidInputException("Oracle VECTOR dimension mismatch: expected %s, got 0", expected_str.c_str());
		}
		return;
	}

	auto parts = StringUtil::Split(value, ',');
	elements.reserve(parts.size());
	for (auto &part : parts) {
		StringUtil::Trim(part);
		if (part.empty()) {
			throw InvalidInputException("empty VECTOR element");
		}
		double val = 0;
		if (!TryCast::Operation<string_t, double>(string_t(part.data(), part.size()), val)) {
			throw InvalidInputException("expected numeric VECTOR element: %s", DisplayValue(part).c_str());
		}
		if (!std::isfinite(val)) {
			throw InvalidInputException("NaN and Infinity are not supported for VECTOR reads");
		}
		if (element_type.id() == LogicalTypeId::DOUBLE) {
			elements.push_back(Value::DOUBLE(val));
		} else {
			float fval = static_cast<float>(val);
			if (!std::isfinite(fval)) {
				throw InvalidInputException("VECTOR FLOAT element out of range: %s", DisplayValue(part).c_str());
			}
			elements.push_back(Value::FLOAT(fval));
		}
	}
	if (expected_dims > 0 && elements.size() != expected_dims) {
		auto expected_str = std::to_string(expected_dims);
		auto actual_str = std::to_string(elements.size());
		throw InvalidInputException("Oracle VECTOR dimension mismatch: expected %s, got %s", expected_str.c_str(),
		                            actual_str.c_str());
	}
}

static Value ParseVectorJsonToList(const string &json_str, const LogicalType &target_type,
                                   const OracleConversionContext &context) {
	const auto &child_type = ListType::GetChildType(target_type);
	vector<Value> elements;
	try {
		ParseOracleVectorElements(json_str, child_type, 0, elements);
	} catch (const std::exception &ex) {
		ThrowConversionError(context, json_str, ex.what());
	}
	return Value::LIST(child_type, std::move(elements));
}

static void SetStringLikeValue(Vector &output_vector, idx_t row_index, const char *data, ub2 length) {
	auto value = ReadOracleString(data, length);
	auto target_data = FlatVector::GetData<string_t>(output_vector);
	if (output_vector.GetType().id() == LogicalTypeId::BLOB) {
		target_data[row_index] = StringVector::AddStringOrBlob(output_vector, string_t(value.data(), value.size()));
	} else {
		target_data[row_index] = StringVector::AddString(output_vector, value);
	}
}

void SetOracleOutputValue(ClientContext &context, Vector &output_vector, idx_t row_index, const char *data, ub2 length,
                          const LogicalType &target_type, const OracleConversionContext &conversion_context) {
	switch (target_type.id()) {
	case LogicalTypeId::VARCHAR:
	case LogicalTypeId::BLOB:
		SetStringLikeValue(output_vector, row_index, data, length);
		break;
	case LogicalTypeId::BIGINT: {
		auto value = TrimmedText(data, length);
		FlatVector::GetData<int64_t>(output_vector)[row_index] = ParseBigInt(value, conversion_context);
		break;
	}
	case LogicalTypeId::FLOAT: {
		auto value = TrimmedText(data, length);
		FlatVector::GetData<float>(output_vector)[row_index] = ParseFloat(value, conversion_context);
		break;
	}
	case LogicalTypeId::DOUBLE: {
		auto value = TrimmedText(data, length);
		FlatVector::GetData<double>(output_vector)[row_index] = ParseDouble(value, conversion_context);
		break;
	}
	case LogicalTypeId::DECIMAL: {
		auto value = TrimmedText(data, length);
		output_vector.SetValue(row_index, ParseDecimal(value, target_type, conversion_context));
		break;
	}
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_TZ: {
		auto value = TrimmedText(data, length);
		FlatVector::GetData<timestamp_t>(output_vector)[row_index] = ParseTimestamp(value, conversion_context);
		break;
	}
	case LogicalTypeId::LIST: {
		auto value = ReadOracleString(data, length);
		output_vector.SetValue(row_index, ParseVectorJsonToList(value, target_type, conversion_context));
		break;
	}
	case LogicalTypeId::GEOMETRY: {
		auto value = ReadOracleString(data, length);
		try {
			output_vector.SetValue(row_index, Value(value).CastAs(context, target_type));
		} catch (const std::exception &ex) {
			throw InvalidInputException(
			    "%s: GEOMETRY conversion failed; load the spatial extension or verify valid WKT input. Value: \"%s\". "
			    "Cause: %s",
			    ConversionPrefix(conversion_context).c_str(), DisplayValue(value).c_str(), ex.what());
		}
		break;
	}
	default: {
		auto value = ReadOracleString(data, length);
		try {
			output_vector.SetValue(row_index, Value(value).DefaultCastAs(target_type));
		} catch (const std::exception &ex) {
			ThrowConversionError(conversion_context, value, "unsupported target conversion: " + string(ex.what()));
		}
		break;
	}
	}
}

} // namespace duckdb
