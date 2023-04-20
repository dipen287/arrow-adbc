// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <utility>

#include <gtest/gtest.h>
#include <nanoarrow/nanoarrow.h>

#include "postgres_type.h"

using adbcpq::PostgresType;
using adbcpq::PostgresTypeId;
using adbcpq::PostgresTypeResolver;

class MockTypeResolver : public PostgresTypeResolver {
 public:
  ArrowErrorCode Init() {
    auto all_types = adbcpq::PostgresTypeIdAll(false);
    PostgresTypeResolver::Item item;
    item.oid = 0;

    // Insert all the base types
    for (auto type_id : all_types) {
      std::string typreceive = adbcpq::PostgresTyprecv(type_id);
      std::string typname = adbcpq::PostgresTypname(type_id);
      item.oid++;
      item.typname = typname.c_str();
      item.typreceive = typreceive.c_str();
      NANOARROW_RETURN_NOT_OK(Insert(item, nullptr));
    }

    // Insert one of each nested type
    item.oid++;
    item.typname = "_bool";
    item.typreceive = "array_recv";
    item.child_oid = GetOID(PostgresTypeId::kBool);
    NANOARROW_RETURN_NOT_OK(Insert(item, nullptr));

    item.oid++;
    item.typname = "boolrange";
    item.typreceive = "range_recv";
    item.base_oid = GetOID(PostgresTypeId::kBool);
    NANOARROW_RETURN_NOT_OK(Insert(item, nullptr));

    item.oid++;
    item.typname = "custombool";
    item.typreceive = "domain_recv";
    item.base_oid = GetOID(PostgresTypeId::kBool);
    NANOARROW_RETURN_NOT_OK(Insert(item, nullptr));

    item.oid++;
    uint32_t class_oid = item.oid;
    std::vector<std::pair<std::string, uint32_t>> record_fields = {
        {"int4_col", GetOID(PostgresTypeId::kInt4)},
        {"text_col", GetOID(PostgresTypeId::kText)}};
    InsertClass(class_oid, std::move(record_fields));

    item.oid++;
    item.typname = "customrecord";
    item.typreceive = "record_recv";
    item.class_oid = class_oid;

    NANOARROW_RETURN_NOT_OK(Insert(item, nullptr));
    return NANOARROW_OK;
  }
};

TEST(PostgresTypeTest, PostgresTypeBasic) {
  PostgresType type(PostgresTypeId::kBool);
  EXPECT_EQ(type.field_name(), "");
  EXPECT_EQ(type.typname(), "");
  EXPECT_EQ(type.type_id(), PostgresTypeId::kBool);
  EXPECT_EQ(type.oid(), 0);
  EXPECT_EQ(type.n_children(), 0);

  PostgresType with_info = type.WithPgTypeInfo(1234, "some_typename");
  EXPECT_EQ(with_info.oid(), 1234);
  EXPECT_EQ(with_info.typname(), "some_typename");
  EXPECT_EQ(with_info.type_id(), type.type_id());

  PostgresType with_name = type.WithFieldName("some name");
  EXPECT_EQ(with_name.field_name(), "some name");
  EXPECT_EQ(with_name.oid(), type.oid());
  EXPECT_EQ(with_name.type_id(), type.type_id());

  PostgresType array = type.Array(12345, "array type name");
  EXPECT_EQ(array.oid(), 12345);
  EXPECT_EQ(array.typname(), "array type name");
  EXPECT_EQ(array.n_children(), 1);
  EXPECT_EQ(array.child(0).oid(), type.oid());
  EXPECT_EQ(array.child(0).type_id(), type.type_id());

  PostgresType range = type.Range(12345, "range type name");
  EXPECT_EQ(range.oid(), 12345);
  EXPECT_EQ(range.typname(), "range type name");
  EXPECT_EQ(range.n_children(), 1);
  EXPECT_EQ(range.child(0).oid(), type.oid());
  EXPECT_EQ(range.child(0).type_id(), type.type_id());

  PostgresType domain = type.Domain(123456, "domain type name");
  EXPECT_EQ(domain.oid(), 123456);
  EXPECT_EQ(domain.typname(), "domain type name");
  EXPECT_EQ(domain.type_id(), type.type_id());

  PostgresType record(PostgresTypeId::kRecord);
  record.AppendChild("col1", type);
  EXPECT_EQ(record.type_id(), PostgresTypeId::kRecord);
  EXPECT_EQ(record.n_children(), 1);
  EXPECT_EQ(record.child(0).type_id(), type.type_id());
  EXPECT_EQ(record.child(0).field_name(), "col1");
}

TEST(PostgresTypeTest, PostgresTypeSetSchema) {
  ArrowSchema schema;

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kBool).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "b");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kInt2).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "s");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kInt4).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "i");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kInt8).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "l");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kFloat4).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "f");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kFloat8).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "g");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kText).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "u");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kBytea).SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "z");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  EXPECT_EQ(PostgresType(PostgresTypeId::kBool).Array().SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "+l");
  EXPECT_STREQ(schema.children[0]->format, "b");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  PostgresType record(PostgresTypeId::kRecord);
  record.AppendChild("col1", PostgresType(PostgresTypeId::kBool));
  EXPECT_EQ(record.SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "+s");
  EXPECT_STREQ(schema.children[0]->format, "b");
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  PostgresType unknown(PostgresTypeId::kBrinMinmaxMultiSummary);
  EXPECT_EQ(unknown.WithPgTypeInfo(0, "some_name").SetSchema(&schema), NANOARROW_OK);
  EXPECT_STREQ(schema.format, "z");

  ArrowStringView value = ArrowCharView("<not found>");
  ArrowMetadataGetValue(schema.metadata, ArrowCharView("ADBC:postgresql:typname"),
                        &value);
  EXPECT_EQ(std::string(value.data, value.size_bytes), "some_name");
  schema.release(&schema);
}

TEST(PostgresTypeTest, PostgresTypeFromSchema) {
  ArrowSchema schema;
  PostgresType type;
  MockTypeResolver resolver;
  ASSERT_EQ(resolver.Init(), NANOARROW_OK);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_BOOL), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kBool);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_INT8), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kInt2);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_UINT8), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kInt2);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_INT16), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kInt2);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_UINT16), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kInt4);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_INT32), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kInt4);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_UINT32), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kInt8);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_INT64), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kInt8);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_FLOAT), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kFloat4);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_DOUBLE), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kFloat8);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_BINARY), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kBytea);
  schema.release(&schema);

  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_STRING), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kText);
  schema.release(&schema);

  ArrowSchemaInit(&schema);
  ASSERT_EQ(ArrowSchemaSetType(&schema, NANOARROW_TYPE_LIST), NANOARROW_OK);
  ASSERT_EQ(ArrowSchemaSetType(schema.children[0], NANOARROW_TYPE_BOOL), NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.type_id(), PostgresTypeId::kArray);
  EXPECT_EQ(type.child(0).type_id(), PostgresTypeId::kBool);
  schema.release(&schema);

  ArrowError error;
  ASSERT_EQ(ArrowSchemaInitFromType(&schema, NANOARROW_TYPE_INTERVAL_MONTH_DAY_NANO),
            NANOARROW_OK);
  EXPECT_EQ(adbcpq::PostgresType::FromSchema(resolver, &schema, &type, &error), ENOTSUP);
  EXPECT_STREQ(error.message,
               "Can't map Arrow type 'interval_month_day_nano' to Postgres type");
  schema.release(&schema);
}

TEST(PostgresTypeTest, PostgresTypeResolver) {
  PostgresTypeResolver resolver;
  ArrowError error;
  PostgresType type;
  PostgresTypeResolver::Item item;

  // Check error for type not found
  EXPECT_EQ(resolver.Find(123, &type, &error), EINVAL);
  EXPECT_STREQ(ArrowErrorMessage(&error), "Postgres type with oid 123 not found");

  // Check error for unsupported type_id name
  item.oid = 123;
  item.typname = "invalid";
  item.typreceive = "invalid_recv";
  EXPECT_EQ(resolver.Insert(item, &error), ENOTSUP);
  EXPECT_STREQ(
      ArrowErrorMessage(&error),
      "Base type not found for type 'invalid' with receive function 'invalid_recv'");

  // Check error for Array with unknown child
  item.typname = "some_array";
  item.typreceive = "array_recv";
  item.child_oid = 1234;
  EXPECT_EQ(resolver.Insert(item, &error), EINVAL);
  EXPECT_STREQ(ArrowErrorMessage(&error), "Postgres type with oid 1234 not found");

  // Check error for Range with unknown child
  item.typname = "some_range";
  item.typreceive = "range_recv";
  item.base_oid = 12345;
  EXPECT_EQ(resolver.Insert(item, &error), EINVAL);
  EXPECT_STREQ(ArrowErrorMessage(&error), "Postgres type with oid 12345 not found");

  // Check error for Domain with unknown child
  item.typname = "some_domain";
  item.typreceive = "domain_recv";
  item.base_oid = 123456;
  EXPECT_EQ(resolver.Insert(item, &error), EINVAL);
  EXPECT_STREQ(ArrowErrorMessage(&error), "Postgres type with oid 123456 not found");

  // Check error for Record with unknown class
  item.typname = "some_record";
  item.typreceive = "record_recv";
  item.class_oid = 123456;
  EXPECT_EQ(resolver.Insert(item, &error), EINVAL);
  EXPECT_STREQ(ArrowErrorMessage(&error), "Class definition with oid 123456 not found");

  // Check insert/resolve of regular type
  item.typname = "some_type_name";
  item.typreceive = "boolrecv";
  item.oid = 10;
  EXPECT_EQ(resolver.Insert(item, &error), NANOARROW_OK);
  EXPECT_EQ(resolver.Find(10, &type, &error), NANOARROW_OK);
  EXPECT_EQ(type.oid(), 10);
  EXPECT_EQ(type.typname(), "some_type_name");
  EXPECT_EQ(type.type_id(), PostgresTypeId::kBool);

  // Check insert/resolve of array type
  item.oid = 11;
  item.typname = "some_array_type_name";
  item.typreceive = "array_recv";
  item.child_oid = 10;
  EXPECT_EQ(resolver.Insert(item, &error), NANOARROW_OK);
  EXPECT_EQ(resolver.Find(11, &type, &error), NANOARROW_OK);
  EXPECT_EQ(type.oid(), 11);
  EXPECT_EQ(type.typname(), "some_array_type_name");
  EXPECT_EQ(type.type_id(), PostgresTypeId::kArray);
  EXPECT_EQ(type.child(0).oid(), 10);
  EXPECT_EQ(type.child(0).type_id(), PostgresTypeId::kBool);

  // Check reverse lookup of array type from item type
  EXPECT_EQ(resolver.FindArray(10, &type, &error), NANOARROW_OK);
  EXPECT_EQ(type.oid(), 11);

  // Check insert/resolve of range type
  item.oid = 12;
  item.typname = "some_range_type_name";
  item.typreceive = "range_recv";
  item.base_oid = 10;
  EXPECT_EQ(resolver.Insert(item, &error), NANOARROW_OK);
  EXPECT_EQ(resolver.Find(12, &type, &error), NANOARROW_OK);
  EXPECT_EQ(type.oid(), 12);
  EXPECT_EQ(type.typname(), "some_range_type_name");
  EXPECT_EQ(type.type_id(), PostgresTypeId::kRange);
  EXPECT_EQ(type.child(0).oid(), 10);
  EXPECT_EQ(type.child(0).type_id(), PostgresTypeId::kBool);

  // Check insert/resolve of domain type
  item.oid = 13;
  item.typname = "some_domain_type_name";
  item.typreceive = "domain_recv";
  item.base_oid = 10;
  EXPECT_EQ(resolver.Insert(item, &error), NANOARROW_OK);
  EXPECT_EQ(resolver.Find(13, &type, &error), NANOARROW_OK);
  EXPECT_EQ(type.oid(), 13);
  EXPECT_EQ(type.typname(), "some_domain_type_name");
  EXPECT_EQ(type.type_id(), PostgresTypeId::kBool);
}

TEST(PostgresTypeTest, PostgresTypeResolveRecord) {
  // Use the mock resolver for the record test since it already has one
  MockTypeResolver resolver;
  ASSERT_EQ(resolver.Init(), NANOARROW_OK);

  PostgresType type;
  EXPECT_EQ(resolver.Find(resolver.GetOID(PostgresTypeId::kRecord), &type, nullptr),
            NANOARROW_OK);
  EXPECT_EQ(type.oid(), resolver.GetOID(PostgresTypeId::kRecord));
  EXPECT_EQ(type.n_children(), 2);
  EXPECT_EQ(type.child(0).field_name(), "int4_col");
  EXPECT_EQ(type.child(0).type_id(), PostgresTypeId::kInt4);
  EXPECT_EQ(type.child(1).field_name(), "text_col");
  EXPECT_EQ(type.child(1).type_id(), PostgresTypeId::kText);
}
