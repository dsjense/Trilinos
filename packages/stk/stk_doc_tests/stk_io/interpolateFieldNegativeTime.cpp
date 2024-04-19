// Copyright 2002 - 2008, 2010, 2011 National Technology Engineering
// Solutions of Sandia, LLC (NTESS). Under the terms of Contract
// DE-NA0003525 with NTESS, the U.S. Government retains certain rights
// in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// 
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
// 
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
// 
//     * Neither the name of NTESS nor the names of its contributors
//       may be used to endorse or promote products derived from this
//       software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 

#include <gtest/gtest.h>                // for AssertHelper, etc
#include <stddef.h>                     // for size_t
#include <unistd.h>                     // for unlink
#include <iomanip>                      // for operator<<
#include <stk_io/StkMeshIoBroker.hpp>   // for StkMeshIoBroker
#include <stk_mesh/base/Field.hpp>      // for Field
#include <stk_mesh/base/FieldBLAS.hpp>
#include <stk_mesh/base/ForEachEntity.hpp>
#include <stk_mesh/base/MetaData.hpp>   // for MetaData, put_field
#include <string>                       // for string
#include <vector>                       // for vector
#include "stk_io/DatabasePurpose.hpp"   // for DatabasePurpose::READ_MESH, etc
#include "stk_io/MeshField.hpp"         // for MeshField, etc
#include "stk_mesh/base/Entity.hpp"     // for Entity
#include "stk_mesh/base/FieldBase.hpp"  // for field_data
#include "stk_topology/topology.hpp"    // for topology, etc
#include "stk_util/parallel/Parallel.hpp"

namespace {

TEST(StkMeshIoBrokerHowTo, interpolateFieldNegativeTime)
{
  std::string ic_name = "interpolate_field_example.e";
  MPI_Comm communicator = MPI_COMM_WORLD;
  int numProcs = stk::parallel_machine_size(communicator);
  if (numProcs != 1) {
    return;
  }

  {
    // ============================================================
    //+ INITIALIZATION:
    //+ Create a mesh with the nodal field "temp" for 3 timesteps.
    //+ The times on the database are -2.0, -1.0, and 0.0
    //+ The value of the field at each node is 0.0 at time 0.0,
    //+ -1.0 at time -1.0, and -2.0 at time -2.0
    stk::io::StkMeshIoBroker stkIo(communicator);
    stkIo.use_simple_fields();

    const std::string generatedFileName = "generated:8x8x8|nodeset:xyz";
    stkIo.add_mesh_database(generatedFileName, stk::io::READ_MESH);
    stkIo.create_input_mesh();

    stk::mesh::Field<double> &temperature =
        stkIo.meta_data().declare_field<double>(stk::topology::NODE_RANK, "temperature", 1);
    stk::mesh::put_field_on_mesh(temperature, stkIo.meta_data().universal_part(), nullptr);
    stkIo.populate_bulk_data();

    size_t fh = stkIo.create_output_mesh(ic_name, stk::io::WRITE_RESULTS);

    //+ The name of the field on the database will be "temp"
    stkIo.add_field(fh, temperature, "temp");

    // Add three steps to the database
    // For each step, the value of the field is the value 'time'
    for (int i=-2; i <= 0; i++) {
      double time = i;

      stk::mesh::field_fill(time, temperature);

      stkIo.begin_output_step(fh, time);
      stkIo.write_defined_output_fields(fh);
      stkIo.end_output_step(fh);
    }
  }

  {
    //-BEGIN
    // ============================================================
    //+ EXAMPLE:
    //+ The input mesh database has 3 timesteps with times -2.0, -1.0, 0.0.
    //+ The value of the field "temp" is equal to the time
    //+ Read the "temp" value at times -2.0 to 0.0 with an interval
    //+ of 0.1 (-2.0, -1.9, -1.8, ..., 0.0) and verify that
    //+ the field contains the correct interpolated value.
    stk::io::StkMeshIoBroker stkIo(communicator);
    stkIo.use_simple_fields();
    stkIo.add_mesh_database(ic_name, stk::io::READ_MESH);
    stkIo.create_input_mesh();

    stk::mesh::Field<double> &temperature =
        stkIo.meta_data().declare_field<double>(stk::topology::NODE_RANK, "temperature", 1);
    stk::mesh::put_field_on_mesh(temperature, stkIo.meta_data().universal_part(), nullptr);

    stkIo.populate_bulk_data();

    // The name of the field on the database is "temp"
    stkIo.add_input_field(stk::io::MeshField(temperature, "temp",
                                             stk::io::MeshField::LINEAR_INTERPOLATION));

    for (int i=-20; i <= 0; i++) {
      double time = i/10.0;
      //+ Read the field values from the database and verify that they
      //+ are interpolated correctly.
      stkIo.read_defined_input_fields(time);

      // ============================================================
      //+ VERIFICATION
      // The value of the "temperature" field at all nodes should be 'time'
      stk::mesh::for_each_entity_run(stkIo.bulk_data(), stk::topology::NODE_RANK,
        [&](const stk::mesh::BulkData& bulk, stk::mesh::Entity node) {
          double *fieldData = stk::mesh::field_data(temperature, node);
          EXPECT_DOUBLE_EQ(time, *fieldData);
        });
    }
    //-END
  }
  // ============================================================
  // Cleanup
  unlink(ic_name.c_str());
}
}
