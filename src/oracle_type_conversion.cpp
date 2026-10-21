#include "oracle_type_conversion.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/operator/cast_operators.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <cmath>

namespace duckdb {

struct OracleSlice {
	const char *ptr;
	size_t len;
};

static inline OracleSlice RawSlice(const char *data, ub2 length) {
	if (!data || length == 0) {
		return {"", 0};
	}
	size_t actual_len = (data[length - 1] == '\0') ? static_cast<size_t>(length - 1) : static_cast<size_t>(length);
	return {data, actual_len};
}

static inline OracleSlice TrimmedSlice(const char *data, ub2 length) {
	auto s = RawSlice(data, length);
	while (s.len > 0 && StringUtil::CharacterIsSpace(*s.ptr)) {
		s.ptr++;
		s.len--;
	}
	while (s.len > 0 && StringUtil::CharacterIsSpace(s.ptr[s.len - 1])) {
		s.len--;
	}
	return s;
}

static string ReadOracleString(const char *data, ub2 length) {
	auto s = RawSlice(data, length);
	return string(s.ptr, s.len);
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
	auto s = TrimmedSlice(data, length);
	return string(s.ptr, s.len);
}

bool TryParseOracleMetadataInt(const string &raw, int32_t &out) {
	auto s = TrimmedSlice(raw.data(), static_cast<ub2>(MinValue<size_t>(raw.size(), NumericLimits<ub2>::Maximum())));
	if (s.len == 0) {
		return false;
	}
	return TryCast::Operation<string_t, int32_t>(string_t(s.ptr, static_cast<uint32_t>(s.len)), out);
}

bool TryParseOracleMetadataIndex(const string &raw, idx_t &out) {
	auto s = TrimmedSlice(raw.data(), static_cast<ub2>(MinValue<size_t>(raw.size(), NumericLimits<ub2>::Maximum())));
	if (s.len == 0 || s.ptr[0] == '-') {
		return false;
	}
	uint64_t parsed = 0;
	if (!TryCast::Operation<string_t, uint64_t>(string_t(s.ptr, static_cast<uint32_t>(s.len)), parsed)) {
		return false;
	}
	out = static_cast<idx_t>(parsed);
	return true;
}

static int64_t ParseBigInt(OracleSlice slice, const OracleConversionContext &context) {
	int64_t result = 0;
	if (slice.len == 0 ||
	    !TryCast::Operation<string_t, int64_t>(string_t(slice.ptr, static_cast<uint32_t>(slice.len)), result)) {
		ThrowConversionError(context, string(slice.ptr, slice.len), "expected BIGINT literal");
	}
	return result;
}

static float ParseFloat(OracleSlice slice, const OracleConversionContext &context) {
	double dval = 0;
	if (slice.len == 0 ||
	    !TryCast::Operation<string_t, double>(string_t(slice.ptr, static_cast<uint32_t>(slice.len)), dval)) {
		ThrowConversionError(context, string(slice.ptr, slice.len), "expected FLOAT literal");
	}
	float result = static_cast<float>(dval);
	if (std::isfinite(dval) && !std::isfinite(result)) {
		ThrowConversionError(context, string(slice.ptr, slice.len), "FLOAT literal out of range");
	}
	return result;
}

static double ParseDouble(OracleSlice slice, const OracleConversionContext &context) {
	double result = 0;
	if (slice.len == 0 ||
	    !TryCast::Operation<string_t, double>(string_t(slice.ptr, static_cast<uint32_t>(slice.len)), result)) {
		ThrowConversionError(context, string(slice.ptr, slice.len), "expected DOUBLE literal");
	}
	return result;
}

static timestamp_t ParseTimestamp(OracleSlice slice, const OracleConversionContext &context) {
	if (slice.len == 0) {
		ThrowConversionError(context, string(), "expected TIMESTAMP literal");
	}
	timestamp_t result;
	if (Timestamp::TryConvertTimestamp(slice.ptr, slice.len, result, true) == TimestampCastResult::SUCCESS) {
		return result;
	}
	string value(slice.ptr, slice.len);
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
	const char *p = text.data();
	const char *end = p + text.size();
	while (p < end && StringUtil::CharacterIsSpace(*p)) {
		p++;
	}
	while (end > p && StringUtil::CharacterIsSpace(*(end - 1))) {
		end--;
	}
	if (end - p < 2 || *p != '[' || *(end - 1) != ']') {
		throw InvalidInputException("expected VECTOR_SERIALIZE JSON array");
	}
	p++;
	end--;
	while (p < end && StringUtil::CharacterIsSpace(*p)) {
		p++;
	}
	while (end > p && StringUtil::CharacterIsSpace(*(end - 1))) {
		end--;
	}
	if (p == end) {
		if (expected_dims > 0) {
			auto expected_str = std::to_string(expected_dims);
			throw InvalidInputException("Oracle VECTOR dimension mismatch: expected %s, got 0", expected_str.c_str());
		}
		return;
	}
	if (expected_dims > 0) {
		elements.reserve(expected_dims);
	}

	while (true) {
		const char *tok_start = p;
		while (p < end && *p != ',') {
			p++;
		}
		const char *tok_end = p;
		while (tok_start < tok_end && StringUtil::CharacterIsSpace(*tok_start)) {
			tok_start++;
		}
		while (tok_end > tok_start && StringUtil::CharacterIsSpace(*(tok_end - 1))) {
			tok_end--;
		}
		if (tok_start == tok_end) {
			throw InvalidInputException("empty VECTOR element");
		}
		double val = 0;
		size_t tok_len = static_cast<size_t>(tok_end - tok_start);
		if (!TryCast::Operation<string_t, double>(string_t(tok_start, static_cast<uint32_t>(tok_len)), val)) {
			throw InvalidInputException("expected numeric VECTOR element: %s",
			                            DisplayValue(string(tok_start, tok_len)).c_str());
		}
		if (!std::isfinite(val)) {
			throw InvalidInputException("NaN and Infinity are not supported for VECTOR reads");
		}
		if (element_type.id() == LogicalTypeId::DOUBLE) {
			elements.push_back(Value::DOUBLE(val));
		} else {
			float fval = static_cast<float>(val);
			if (!std::isfinite(fval)) {
				throw InvalidInputException("VECTOR FLOAT element out of range: %s",
				                            DisplayValue(string(tok_start, tok_len)).c_str());
			}
			elements.push_back(Value::FLOAT(fval));
		}
		if (p == end) {
			break;
		}
		p++;
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
	auto slice = RawSlice(data, length);
	auto target_data = FlatVector::GetDataMutable<string_t>(output_vector);
	if (output_vector.GetType().id() == LogicalTypeId::BLOB) {
		target_data[row_index] =
		    StringVector::AddStringOrBlob(output_vector, string_t(slice.ptr, static_cast<uint32_t>(slice.len)));
	} else {
		target_data[row_index] = StringVector::AddString(output_vector, slice.ptr, slice.len);
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
		auto slice = TrimmedSlice(data, length);
		FlatVector::GetDataMutable<int64_t>(output_vector)[row_index] = ParseBigInt(slice, conversion_context);
		break;
	}
	case LogicalTypeId::FLOAT: {
		auto slice = TrimmedSlice(data, length);
		FlatVector::GetDataMutable<float>(output_vector)[row_index] = ParseFloat(slice, conversion_context);
		break;
	}
	case LogicalTypeId::DOUBLE: {
		auto slice = TrimmedSlice(data, length);
		FlatVector::GetDataMutable<double>(output_vector)[row_index] = ParseDouble(slice, conversion_context);
		break;
	}
	case LogicalTypeId::DECIMAL: {
		auto value = TrimmedText(data, length);
		output_vector.SetValue(row_index, ParseDecimal(value, target_type, conversion_context));
		break;
	}
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_TZ: {
		auto slice = TrimmedSlice(data, length);
		FlatVector::GetDataMutable<timestamp_t>(output_vector)[row_index] = ParseTimestamp(slice, conversion_context);
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
