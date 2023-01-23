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

package flightsql

import (
	"context"

	"github.com/apache/arrow-adbc/go/adbc"
	"github.com/apache/arrow/go/v11/arrow"
	"github.com/apache/arrow/go/v11/arrow/array"
	"github.com/apache/arrow/go/v11/arrow/flight"
	"github.com/apache/arrow/go/v11/arrow/flight/flightsql"
	"github.com/apache/arrow/go/v11/arrow/memory"
	"github.com/bluele/gcache"
	"google.golang.org/protobuf/proto"
)

type statement struct {
	alloc       memory.Allocator
	cl          *flightsql.Client
	clientCache gcache.Cache

	query    string
	prepared *flightsql.PreparedStatement
}

func (s *statement) closePreparedStatement() error {
	return s.prepared.Close(context.Background())
}

// Close releases any relevant resources associated with this statement
// and closes it (particularly if it is a prepared statement).
//
// A statement instance should not be used after Close is called.
func (s *statement) Close() (err error) {
	if s.prepared != nil {
		err = s.closePreparedStatement()
		s.prepared = nil
	}

	if s.cl == nil {
		return adbc.Error{
			Msg:  "[Flight SQL Statement] cannot close already closed statement",
			Code: adbc.StatusInvalidState,
		}
	}

	s.cl = nil
	s.clientCache = nil

	return err
}

// SetOption sets a string option on this statement
func (s *statement) SetOption(key string, val string) error {
	return adbc.Error{
		Msg:  "[FlightSQL Statement] SetOption not implemented",
		Code: adbc.StatusNotImplemented,
	}
}

// SetSqlQuery sets the query string to be executed.
//
// The query can then be executed with any of the Execute methods.
// For queries expected to be executed repeatedly, Prepare should be
// called before execution.
func (s *statement) SetSqlQuery(query string) error {
	if s.prepared != nil {
		if err := s.closePreparedStatement(); err != nil {
			return err
		}
		s.prepared = nil
	}

	s.query = query
	return nil
}

// ExecuteQuery executes the current query or prepared statement
// and returnes a RecordReader for the results along with the number
// of rows affected if known, otherwise it will be -1.
//
// This invalidates any prior result sets on this statement.
func (s *statement) ExecuteQuery(ctx context.Context) (rdr array.RecordReader, nrec int64, err error) {
	var info *flight.FlightInfo
	if s.prepared != nil {
		info, err = s.prepared.Execute(ctx)
	} else if s.query != "" {
		info, err = s.cl.Execute(ctx, s.query)
	} else {
		return nil, -1, adbc.Error{
			Msg:  "[Flight SQL Statement] cannot call ExecuteQuery without a query or prepared statement",
			Code: adbc.StatusInvalidState,
		}
	}

	if err != nil {
		return nil, -1, adbcFromFlightStatus(err)
	}

	nrec = info.TotalRecords
	rdr, err = newRecordReader(ctx, s.alloc, s.cl, info, s.clientCache)
	return
}

// ExecuteUpdate executes a statement that does not generate a result
// set. It returns the number of rows affected if known, otherwise -1.
func (s *statement) ExecuteUpdate(ctx context.Context) (int64, error) {
	if s.prepared != nil {
		return s.prepared.ExecuteUpdate(ctx)
	}

	if s.query != "" {
		return s.cl.ExecuteUpdate(ctx, s.query)
	}

	return -1, adbc.Error{
		Msg:  "[Flight SQL Statement] cannot call ExecuteUpdate without a query or prepared statement",
		Code: adbc.StatusInvalidState,
	}
}

// Prepare turns this statement into a prepared statement to be executed
// multiple times. This invalidates any prior result sets.
func (s *statement) Prepare(ctx context.Context) error {
	if s.query == "" {
		return adbc.Error{
			Msg:  "[FlightSQL Statement] must call SetSqlQuery before Prepare",
			Code: adbc.StatusInvalidState,
		}
	}

	prep, err := s.cl.Prepare(ctx, s.alloc, s.query)
	if err != nil {
		return adbcFromFlightStatus(err)
	}
	s.prepared = prep
	return nil
}

// SetSubstraitPlan allows setting a serialized Substrait execution
// plan into the query or for querying Substrait-related metadata.
//
// Drivers are not required to support both SQL and Substrait semantics.
// If they do, it may be via converting between representations internally.
//
// Like SetSqlQuery, after this is called the query can be executed
// using any of the Execute methods. If the query is expected to be
// executed repeatedly, Prepare should be called first on the statement.
func (s *statement) SetSubstraitPlan(plan []byte) error {
	return adbc.Error{
		Msg:  "[FlightSQL Statement] SetSubstraitPlan not implemented",
		Code: adbc.StatusNotImplemented,
	}
}

// Bind uses an arrow record batch to bind parameters to the query.
//
// This can be used for bulk inserts or for prepared statements.
// The driver will call release on the passed in Record when it is done,
// but it may not do this until the statement is closed or another
// record is bound.
func (s *statement) Bind(_ context.Context, values arrow.Record) error {
	// TODO: handle bulk insert situation

	if s.prepared == nil {
		return adbc.Error{
			Msg:  "[Flight SQL Statement] must call Prepare before calling Bind",
			Code: adbc.StatusInvalidState}
	}

	s.prepared.SetParameters(values)
	return nil
}

// BindStream uses a record batch stream to bind parameters for this
// query. This can be used for bulk inserts or prepared statements.
//
// The driver will call Release on the record reader, but may not do this
// until Close is called.
func (s *statement) BindStream(ctx context.Context, stream array.RecordReader) error {
	return adbc.Error{
		Msg:  "[Flight SQL Statement] BindStream not yet implemented",
		Code: adbc.StatusNotImplemented,
	}
}

// GetParameterSchema returns an Arrow schema representation of
// the expected parameters to be bound.
//
// This retrieves an Arrow Schema describing the number, names, and
// types of the parameters in a parameterized statement. The fields
// of the schema should be in order of the ordinal position of the
// parameters; named parameters should appear only once.
//
// If the parameter does not have a name, or a name cannot be determined,
// the name of the corresponding field in the schema will be an empty
// string. If the type cannot be determined, the type of the corresponding
// field will be NA (NullType).
//
// This should be called only after calling Prepare.
//
// This should return an error with StatusNotImplemented if the schema
// cannot be determined.
func (s *statement) GetParameterSchema() (*arrow.Schema, error) {
	if s.prepared == nil {
		return nil, adbc.Error{
			Msg:  "[Flight SQL Statement] must call Prepare before GetParameterSchema",
			Code: adbc.StatusInvalidState,
		}
	}

	ret := s.prepared.ParameterSchema()
	if ret == nil {
		return nil, adbc.Error{Code: adbc.StatusNotImplemented}
	}

	return ret, nil
}

// ExecutePartitions executes the current statement and gets the results
// as a partitioned result set.
//
// It returns the Schema of the result set (if available, nil otherwise),
// the collection of partition descriptors and the number of rows affected,
// if known. If unknown, the number of rows affected will be -1.
//
// If the driver does not support partitioned results, this will return
// an error with a StatusNotImplemented code.
func (s *statement) ExecutePartitions(ctx context.Context) (*arrow.Schema, adbc.Partitions, int64, error) {
	var (
		info *flight.FlightInfo
		out  adbc.Partitions
		sc   *arrow.Schema
		err  error
	)

	if s.prepared != nil {
		info, err = s.prepared.Execute(ctx)
	} else if s.query != "" {
		info, err = s.cl.Execute(ctx, s.query)
	} else {
		return nil, out, -1, adbc.Error{
			Msg:  "[Flight SQL Statement] cannot call ExecuteQuery without a query or prepared statement",
			Code: adbc.StatusInvalidState,
		}
	}

	if err != nil {
		return nil, out, -1, adbcFromFlightStatus(err)
	}

	if len(info.Schema) > 0 {
		sc, err = flight.DeserializeSchema(info.Schema, s.alloc)
		if err != nil {
			return nil, out, -1, adbcFromFlightStatus(err)
		}
	}

	out.NumPartitions = uint64(len(info.Endpoint))
	out.PartitionIDs = make([][]byte, out.NumPartitions)
	for i, e := range info.Endpoint {
		data, err := proto.Marshal(e)
		if err != nil {
			return sc, out, -1, adbc.Error{
				Msg:  err.Error(),
				Code: adbc.StatusInternal,
			}
		}

		out.PartitionIDs[i] = data
	}

	return sc, out, info.TotalRecords, nil
}
