#include "oracle_type_conversion.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

static string ReadOracleString(const char *data, ub2 length) {
	if (!data || length == 0) {
		return string();
	}
	string result(data, length);
	auto nul_pos = result.find('\0');
	if (nul_pos != string::npos) {
		result.resize(nul_pos);
	}
	return result;
}

static string ConversionPrefix(const OracleConversionContext &context) {
	return StringUtil::Format("Failed to convert Oracle value for column \"%s\" (Oracle %s -> DuckDB %s, row %llu)",
	                          context.column_name.c_str(), context.oracle_type.c_str(), context.duckdb_type.ToString(),
	                          static_cast<unsigned long long>(context.row_index));
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

static void RequireFullParse(const string &value, size_t parsed, const OracleConversionContext &context,
                             const string &target_name) {
	if (parsed != value.size()) {
		ThrowConversionError(context, value, "expected complete " + target_name + " literal");
	}
}

static int64_t ParseBigInt(const string &value, const OracleConversionContext &context) {
	try {
		size_t parsed = 0;
		auto result = std::stoll(value, &parsed);
		RequireFullParse(value, parsed, context, "BIGINT");
		return result;
	} catch (const std::exception &ex) {
		ThrowConversionError(context, value, "expected BIGINT literal: " + string(ex.what()));
	}
	throw InternalException("unreachable BIGINT conversion path");
}

static double ParseDouble(const string &value, const OracleConversionContext &context) {
	try {
		size_t parsed = 0;
		auto result = std::stod(value, &parsed);
		RequireFullParse(value, parsed, context, "DOUBLE");
		return result;
	} catch (const std::exception &ex) {
		ThrowConversionError(context, value, "expected DOUBLE literal: " + string(ex.what()));
	}
	throw InternalException("unreachable DOUBLE conversion path");
}

static timestamp_t ParseTimestamp(const string &value, const OracleConversionContext &context) {
	if (value.empty()) {
		ThrowConversionError(context, value, "expected TIMESTAMP literal");
	}
	try {
		return Timestamp::FromString(value, false);
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

static float ParseVectorElement(const string &value, const OracleConversionContext &context) {
	try {
		size_t parsed = 0;
		auto result = std::stof(value, &parsed);
		RequireFullParse(value, parsed, context, "FLOAT");
		return result;
	} catch (const std::exception &ex) {
		ThrowConversionError(context, value, "expected VECTOR FLOAT element: " + string(ex.what()));
	}
	throw InternalException("unreachable VECTOR conversion path");
}

static Value ParseVectorJsonToList(const string &json_str, const OracleConversionContext &context) {
	vector<Value> elements;
	auto value = json_str;
	StringUtil::Trim(value);
	if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
		ThrowConversionError(context, json_str, "expected VECTOR_SERIALIZE JSON array");
	}

	value = value.substr(1, value.size() - 2);
	StringUtil::Trim(value);
	if (value.empty()) {
		return Value::LIST(LogicalType::FLOAT, std::move(elements));
	}

	auto parts = StringUtil::Split(value, ',');
	elements.reserve(parts.size());
	for (auto &part : parts) {
		StringUtil::Trim(part);
		if (part.empty()) {
			ThrowConversionError(context, json_str, "empty VECTOR element");
		}
		elements.push_back(Value::FLOAT(ParseVectorElement(part, context)));
	}
	return Value::LIST(LogicalType::FLOAT, std::move(elements));
}

static void SetStringLikeValue(Vector &output_vector, idx_t row_index, const char *data, ub2 length) {
	string_t val(data, length);
	FlatVector::GetData<string_t>(output_vector)[row_index] = StringVector::AddString(output_vector, val);
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
	case LogicalTypeId::TIMESTAMP: {
		auto value = TrimmedText(data, length);
		FlatVector::GetData<timestamp_t>(output_vector)[row_index] = ParseTimestamp(value, conversion_context);
		break;
	}
	case LogicalTypeId::LIST: {
		auto value = ReadOracleString(data, length);
		output_vector.SetValue(row_index, ParseVectorJsonToList(value, conversion_context));
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
