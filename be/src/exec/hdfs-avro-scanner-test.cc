// Copyright 2016 Cloudera Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "exec/hdfs-avro-scanner.h"

#include <gtest/gtest.h>
#include <limits.h>

#include "common/init.h"
#include "exec/read-write-util.h"
#include "runtime/decimal-value.inline.h"
#include "runtime/runtime-state.h"
#include "runtime/string-value.inline.h"

// TODO: CHAR, VARCHAR (IMPALA-3658)

namespace impala {

class HdfsAvroScannerTest : public testing::Test {
 public:
  void TestReadUnionType(int null_union_position, uint8_t* data, int64_t data_len,
      bool expected_is_null, TErrorCode::type expected_error = TErrorCode::OK) {
    // Reset parse_status_
    scanner_.parse_status_ = Status::OK();

    uint8_t* new_data = data;
    bool is_null = -1;
    bool expect_success = expected_error == TErrorCode::OK;

    bool success = scanner_.ReadUnionType(null_union_position, &new_data, data + data_len,
        &is_null);
    EXPECT_EQ(success, expect_success);

    if (success) {
      EXPECT_TRUE(scanner_.parse_status_.ok());
      EXPECT_EQ(is_null, expected_is_null);
      EXPECT_EQ(new_data, data + 1);
    } else {
      EXPECT_EQ(scanner_.parse_status_.code(), expected_error);
    }
  }

  // Templated function for calling different ReadAvro* functions.
  //
  // PrimitiveType is a template parameter so we can pass in int 'slot_byte_size' to
  // ReadAvroDecimal, but otherwise this argument is always the PrimitiveType 'type'
  // argument.
  template<typename T, typename ReadAvroTypeFn, typename PrimitiveType>
  void TestReadAvroType(ReadAvroTypeFn read_fn, PrimitiveType type, uint8_t* data,
      int64_t data_len, T expected_val, int expected_encoded_len,
      TErrorCode::type expected_error = TErrorCode::OK) {
    // Reset parse_status_
    scanner_.parse_status_ = Status::OK();

    uint8_t* new_data = data;
    T slot;
    bool expect_success = expected_error == TErrorCode::OK;

    bool success = (scanner_.*read_fn)(type, &new_data, data + data_len, true, &slot,
        NULL);
    EXPECT_EQ(success, expect_success);

    if (success) {
      EXPECT_TRUE(scanner_.parse_status_.ok());
      EXPECT_EQ(slot, expected_val);
      EXPECT_EQ(new_data, data + expected_encoded_len);
    } else {
      EXPECT_EQ(scanner_.parse_status_.code(), expected_error);
    }
  }

  void TestReadAvroBoolean(uint8_t* data, int64_t data_len, bool expected_val,
      TErrorCode::type expected_error = TErrorCode::OK) {
    TestReadAvroType(&HdfsAvroScanner::ReadAvroBoolean, TYPE_BOOLEAN, data, data_len,
        expected_val, 1, expected_error);
  }

  void TestReadAvroInt32(uint8_t* data, int64_t data_len, int32_t expected_val,
      int expected_encoded_len, TErrorCode::type expected_error = TErrorCode::OK) {
    TestReadAvroType(&HdfsAvroScanner::ReadAvroInt32, TYPE_INT, data, data_len,
        expected_val, expected_encoded_len, expected_error);
    // Test type promotion to long, float, and double
    int64_t expected_bigint = expected_val;
    TestReadAvroType(&HdfsAvroScanner::ReadAvroInt32, TYPE_BIGINT, data, data_len,
        expected_bigint, expected_encoded_len, expected_error);
    float expected_float = expected_val;
    TestReadAvroType(&HdfsAvroScanner::ReadAvroInt32, TYPE_FLOAT, data, data_len,
        expected_float, expected_encoded_len, expected_error);
    double expected_double = expected_val;
    TestReadAvroType(&HdfsAvroScanner::ReadAvroInt32, TYPE_DOUBLE, data, data_len,
        expected_double, expected_encoded_len, expected_error);
  }

  void TestReadAvroInt64(uint8_t* data, int64_t data_len, int64_t expected_val,
      int expected_encoded_len, TErrorCode::type expected_error = TErrorCode::OK) {
    TestReadAvroType(&HdfsAvroScanner::ReadAvroInt64, TYPE_BIGINT, data, data_len,
        expected_val, expected_encoded_len, expected_error);
    // Test type promotion to float and double
    float expected_float = expected_val;
    TestReadAvroType(&HdfsAvroScanner::ReadAvroInt64, TYPE_FLOAT, data, data_len,
        expected_float, expected_encoded_len, expected_error);
    double expected_double = expected_val;
    TestReadAvroType(&HdfsAvroScanner::ReadAvroInt64, TYPE_DOUBLE, data, data_len,
        expected_double, expected_encoded_len, expected_error);
  }

  void TestReadAvroFloat(uint8_t* data, int64_t data_len, float expected_val,
      TErrorCode::type expected_error = TErrorCode::OK) {
    TestReadAvroType(&HdfsAvroScanner::ReadAvroFloat, TYPE_FLOAT, data, data_len,
        expected_val, 4, expected_error);
    // Test type promotion to double
    double expected_double = expected_val;
    TestReadAvroType(&HdfsAvroScanner::ReadAvroFloat, TYPE_DOUBLE, data, data_len,
        expected_double, 4, expected_error);
  }

  void TestReadAvroDouble(uint8_t* data, int64_t data_len, double expected_val,
      TErrorCode::type expected_error = TErrorCode::OK) {
    TestReadAvroType(&HdfsAvroScanner::ReadAvroDouble, TYPE_DOUBLE, data, data_len,
        expected_val, 8, expected_error);
  }

  void TestReadAvroString(uint8_t* data, int64_t data_len, StringValue expected_val,
      int expected_encoded_len, TErrorCode::type expected_error = TErrorCode::OK) {
    TestReadAvroType(&HdfsAvroScanner::ReadAvroString, TYPE_STRING, data, data_len,
        expected_val, expected_encoded_len, expected_error);
  }

  template<typename T>
  void TestReadAvroDecimal(uint8_t* data, int64_t data_len, DecimalValue<T> expected_val,
      int expected_encoded_len, TErrorCode::type expected_error = TErrorCode::OK) {
    TestReadAvroType(&HdfsAvroScanner::ReadAvroDecimal, sizeof(expected_val), data,
        data_len, expected_val, expected_encoded_len, expected_error);
  }

  void TestInt64Val(int64_t val) {
    uint8_t data[100];
    int len = ReadWriteUtil::PutZLong(val, data);
    DCHECK_GT(len, 0);
    TestReadAvroInt64(data, len, val, len);
    TestReadAvroInt64(data, len + 1, val, len);
    TestReadAvroInt64(data, len - 1, -1, -1, TErrorCode::SCANNER_INVALID_INT);
  }

 protected:
  HdfsAvroScanner scanner_;
};

// Tests reading a [<some type>, "null"] union.
TEST_F(HdfsAvroScannerTest, NullUnionTest) {
  uint8_t data[100];
  data[0] = 0;
  TestReadUnionType(0, data, 1, true);
  TestReadUnionType(1, data, 1, false);
  TestReadUnionType(0, data, 10, true);
  TestReadUnionType(1, data, 10, false);
  TestReadUnionType(0, data, 0, false, TErrorCode::AVRO_TRUNCATED_BLOCK);

  data[0] = 2;
  TestReadUnionType(0, data, 1, false);
  TestReadUnionType(1, data, 1, true);
  TestReadUnionType(0, data, 10, false);
  TestReadUnionType(1, data, 10, true);
  TestReadUnionType(0, data, 0, false, TErrorCode::AVRO_TRUNCATED_BLOCK);

  data[0] = 1;
  TestReadUnionType(0, data, 0, false, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadUnionType(0, data, 1, false, TErrorCode::AVRO_INVALID_UNION);

  data[0] = -1;
  TestReadUnionType(0, data, 1, false, TErrorCode::AVRO_INVALID_UNION);
}

TEST_F(HdfsAvroScannerTest, BooleanTest) {
  uint8_t data[100];
  data[0] = 0;
  TestReadAvroBoolean(data, 1, false);
  TestReadAvroBoolean(data, 10, false);
  TestReadAvroBoolean(data, 0, false, TErrorCode::AVRO_TRUNCATED_BLOCK);

  data[0] = 1;
  TestReadAvroBoolean(data, 1, true);
  TestReadAvroBoolean(data, 10, true);
  TestReadAvroBoolean(data, 0, false, TErrorCode::AVRO_TRUNCATED_BLOCK);

  data[0] = 2;
  TestReadAvroBoolean(data, 1, false, TErrorCode::AVRO_INVALID_BOOLEAN);
}

TEST_F(HdfsAvroScannerTest, Int32Test) {
  uint8_t data[100];
  data[0] = 1; // decodes to -1
  TestReadAvroInt32(data, 1, -1, 1);
  TestReadAvroInt32(data, 10, -1, 1);
  TestReadAvroInt32(data, 0, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  data[0] = 2; // decodes to 1
  TestReadAvroInt32(data, 1, 1, 1);
  TestReadAvroInt32(data, 10, 1, 1);
  TestReadAvroInt32(data, 0, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  data[0] = 0x80; // decodes to 64
  data[1] = 0x01;
  TestReadAvroInt32(data, 2, 64, 2);
  TestReadAvroInt32(data, 10, 64, 2);
  TestReadAvroInt32(data, 0, -1, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroInt32(data, 1, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  int len = ReadWriteUtil::PutZInt(INT_MAX, data);
  TestReadAvroInt32(data, len, INT_MAX, len);
  TestReadAvroInt32(data, len + 1, INT_MAX, len);
  TestReadAvroInt32(data, len - 1, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  len = ReadWriteUtil::PutZInt(INT_MIN, data);
  TestReadAvroInt32(data, len, INT_MIN, len);
  TestReadAvroInt32(data, len + 1, INT_MIN, len);
  TestReadAvroInt32(data, len - 1, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  // TODO: we don't handle invalid values (e.g. overflow) (IMPALA-3659)
}

TEST_F(HdfsAvroScannerTest, Int64Test) {
  uint8_t data[100];
  data[0] = 1; // decodes to -1
  TestReadAvroInt64(data, 1, -1, 1);
  TestReadAvroInt64(data, 10, -1, 1);
  TestReadAvroInt64(data, 0, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  data[0] = 2; // decodes to 1
  TestReadAvroInt64(data, 1, 1, 1);
  TestReadAvroInt64(data, 10, 1, 1);
  TestReadAvroInt64(data, 0, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  data[0] = 0x80; // decodes to 64
  data[1] = 0x01;
  TestReadAvroInt64(data, 2, 64, 2);
  TestReadAvroInt64(data, 10, 64, 2);
  TestReadAvroInt64(data, 0, -1, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroInt64(data, 1, -1, -1, TErrorCode::SCANNER_INVALID_INT);

  TestInt64Val(INT_MAX);
  TestInt64Val(INT_MIN);
  TestInt64Val(LLONG_MAX);
  TestInt64Val(LLONG_MIN);

  // TODO: we don't handle invalid values (e.g. overflow) (IMPALA-3659)
}

TEST_F(HdfsAvroScannerTest, FloatTest) {
  uint8_t data[100];
  float f = 1.23456789;
  memcpy(data, &f, sizeof(float));
  TestReadAvroFloat(data, 4, f);
  TestReadAvroFloat(data, 10, f);
  TestReadAvroFloat(data, 0, f, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroFloat(data, 1, f, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroFloat(data, 2, f, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroFloat(data, 3, f, TErrorCode::AVRO_TRUNCATED_BLOCK);
}

TEST_F(HdfsAvroScannerTest, DoubleTest) {
  uint8_t data[100];
  double d = 1.23456789012345;
  memcpy(data, &d, sizeof(double));
  TestReadAvroDouble(data, 8, d);
  TestReadAvroDouble(data, 10, d);
  TestReadAvroDouble(data, 0, d, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDouble(data, 1, d, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDouble(data, 2, d, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDouble(data, 3, d, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDouble(data, 7, d, TErrorCode::AVRO_TRUNCATED_BLOCK);
}

TEST_F(HdfsAvroScannerTest, StringTest) {
  uint8_t data[100];
  const char* s = "hello";
  DCHECK_EQ(strlen(s), 5);
  data[0] = 10; // decodes to 5
  memcpy(&data[1], s, 5);
  StringValue sv(s);
  TestReadAvroString(data, 6, sv, 6);
  TestReadAvroString(data, 10, sv, 6);
  TestReadAvroString(data, 0, sv, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroString(data, 1, sv, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroString(data, 5, sv, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);

  data[0] = 1; // decodes to -1
  TestReadAvroString(data, 10, sv, -1, TErrorCode::AVRO_INVALID_LENGTH);

  data[0] = 0; // decodes to 0
  sv.len = 0;
  TestReadAvroString(data, 1, sv, 1);
  TestReadAvroString(data, 10, sv, 1);
}

TEST_F(HdfsAvroScannerTest, DecimalTest) {
  uint8_t data[100];
  Decimal4Value d4v(123);
  // Unscaled value (123) can be stored in 1 byte
  data[0] = 2; // decodes to 1
  data[1] = 123;
  TestReadAvroDecimal(data, 2, d4v, 2);
  TestReadAvroDecimal(data, 10, d4v, 2);
  TestReadAvroDecimal(data, 0, d4v, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroDecimal(data, 1, d4v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);

  data[0] = 10; // decodes to 5
  TestReadAvroDecimal(data, 10, d4v, -1, TErrorCode::AVRO_INVALID_LENGTH);

  data[0] = 1; // decodes to -1
  TestReadAvroDecimal(data, 10, d4v, -1, TErrorCode::AVRO_INVALID_LENGTH);

  data[0] = 0; // decodes to 0
  TestReadAvroDecimal(data, 10, Decimal4Value(0), 1);

  data[0] = 0x80; // decodes to 64
  data[1] = 0x01;
  TestReadAvroDecimal(data, 100, d4v, -1, TErrorCode::AVRO_INVALID_LENGTH);

  d4v = Decimal4Value(123456789);
  // Unscaled value can be stored in 4 bytes
  data[0] = 8; // decodes to 4
#if __BYTE_ORDER == __LITTLE_ENDIAN
  BitUtil::ByteSwap(&data[1], &d4v.value(), 4);
#else
  memcpy(&data[1], &d4v.value(), 4);
#endif
  TestReadAvroDecimal(data, 5, d4v, 5);
  TestReadAvroDecimal(data, 10, d4v, 5);
  TestReadAvroDecimal(data, 0, d4v, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroDecimal(data, 1, d4v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDecimal(data, 4, d4v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);

  Decimal8Value d8v(1);
  data[0] = 2; // decodes to 1
  data[1] = 1;
  TestReadAvroDecimal(data, 2, d8v, 2);
  TestReadAvroDecimal(data, 10, d8v, 2);
  TestReadAvroDecimal(data, 0, d8v, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroDecimal(data, 1, d8v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);

  d8v = Decimal8Value(123456789012345678);
  data[0] = 16; // decodes to 8
#if __BYTE_ORDER == __LITTLE_ENDIAN
  BitUtil::ByteSwap(&data[1], &d8v.value(), 8);
#else
  memcpy(&data[1], &d8v.value(), 8);
#endif
  TestReadAvroDecimal(data, 9, d8v, 9);
  TestReadAvroDecimal(data, 10, d8v, 9);
  TestReadAvroDecimal(data, 0, d8v, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroDecimal(data, 1, d8v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDecimal(data, 7, d8v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);

  Decimal16Value d16v(1234567890);
  data[0] = 10; // decodes to 5
#if __BYTE_ORDER == __LITTLE_ENDIAN
  BitUtil::ByteSwap(&data[1], &d16v.value(), 5);
#else
  memcpy(&data[1], &d16v.value(), 5);
#endif
  TestReadAvroDecimal(data, 6, d16v, 6);
  TestReadAvroDecimal(data, 10, d16v, 6);
  TestReadAvroDecimal(data, 0, d16v, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroDecimal(data, 1, d16v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDecimal(data, 4, d16v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);

  bool overflow;
  d16v = Decimal16Value::FromDouble(38, 0, .1e38d, &overflow);
  DCHECK(!overflow);
  data[0] = 32; // decodes to 16
#if __BYTE_ORDER == __LITTLE_ENDIAN
  BitUtil::ByteSwap(&data[1], &d16v.value(), 16);
#else
  memcpy(&data[1], &d16v.value(), 16);
#endif
  TestReadAvroDecimal(data, 17, d16v, 17);
  TestReadAvroDecimal(data, 20, d16v, 17);
  TestReadAvroDecimal(data, 0, d16v, -1, TErrorCode::SCANNER_INVALID_INT);
  TestReadAvroDecimal(data, 1, d16v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);
  TestReadAvroDecimal(data, 16, d16v, -1, TErrorCode::AVRO_TRUNCATED_BLOCK);
}

}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  InitCommonRuntime(argc, argv, false, impala::TestInfo::BE_TEST);
  return RUN_ALL_TESTS();
}
