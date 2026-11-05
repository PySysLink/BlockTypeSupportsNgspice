# BlockTypeSupportsNgspice

PySysLink block type support for transient circuits evaluated by ngspice's
shared-library API. Each block dynamically loads its own copied ngspice shared
library, allowing independent instances in one process.

## Requirements and build

The PySysLinkBase development package must be installed. The plugin does not
search for ngspice during configuration or build; each block loads the
library path supplied through `NgspiceLibraryPath`. Build this submodule with:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

To run the integration test, explicitly provide a shared ngspice library path
at configure time:

```sh
cmake -S . -B build -DNGSPICE_TEST_LIBRARY_PATH=/path/to/libngspice.so
cmake --build build
ctest --test-dir build --output-on-failure
```

## Block configuration

The `Ngspice` block has these configuration values:

| Name | Type | Meaning |
| --- | --- | --- |
| `NetlistPath` | string | Path to a netlist containing a `.tran` analysis. |
| `NgspiceLibraryPath` | string | Path to the unique ngspice shared-library copy for this block. |
| `InputSources` | string[] | Ordered independent voltage/current source names driven by input ports. |
| `OutputVectors` | string[] | Ordered ngspice transient vectors read into output ports. |
| `SampleTime` | double | PySysLink discrete step in seconds; `-1` inherits a discrete step. |

Use ngspice's `external` source form for each driven source, for example
`Vdrive in 0 dc 0 external` or `Iload out 0 dc 0 external`. Input ports follow
the order of `InputSources`; output ports follow `OutputVectors`. A netlist
might include:

```spice
Transient RC circuit
Vdrive in 0 dc 0 external
R1 in out 1k
C1 out 0 1u
.tran 1u 10m 0 1u
.end
```

For node voltages request `v(out)`; the block maps this to ngspice's `out`
vector. Branch currents can be requested as `i(Vdrive)` and map to ngspice's
`vdrive#branch` vector. Other device vectors can be requested by their ngspice
names when they are present in the transient plot; use a netlist `save`
directive when needed. The block uses ngspice's synchronization callback to
limit each accepted timestep to the next PySysLink sample endpoint. The
`.tran` output interval need not equal `SampleTime`, but its stop time must
cover the full PySysLink simulation duration. For example, a model running
2000 steps of `0.01` seconds needs a stop time of at least 20 seconds; ngspice
will end the analysis when its `.tran` stop time is reached. At each sample
endpoint, the synchronization callback waits for PySysLink's next inputs
before allowing the transient run to continue.

Netlist paths must not contain whitespace or quotes because of ngspice's
interactive `source` command syntax.

The application that constructs the simulation must make a separate,
byte-for-byte copy of the ngspice shared library for every ngspice block and set
that block's `NgspiceLibraryPath` to its copy. The copies must have distinct
paths and remain on disk until all corresponding blocks have been destroyed.
The block loads and unloads its configured library, but does not create, own,
or delete library copies.

Netlists must not contain a `.control` section. ngspice handles the transient
solver; the block uses ngspice's `GetSyncData` callback to constrain each
accepted step to the requested PySysLink time, then waits for the next block
input before continuing. This avoids changing breakpoints while ngspice's
background thread is running. Circuit rewinds, skipped sample steps, AC/DC
analyses, and synchronization across different circuit partitions are not
supported.