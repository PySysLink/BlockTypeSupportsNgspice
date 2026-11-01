# BlockTypeSupportsNgspice

PySysLink block type support for transient circuits evaluated by the ngspice
library. CMake prefers `libngspice.a` when installed and falls back to the
shared library otherwise; set `NGSPICE_PREFER_STATIC=OFF` to explicitly use the
shared library.

## Requirements and build

An ngspice library and the PySysLinkBase development package must be installed.
Static linking requires a position-independent `libngspice.a` and any
dependencies needed by that archive. Build this submodule with:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

## Block configuration

The `Ngspice` block has these configuration values:

| Name | Type | Meaning |
| --- | --- | --- |
| `NetlistPath` | string | Path to a netlist containing a `.tran` analysis. |
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
directive when needed. The block posts an ngspice breakpoint at each PySysLink
sample endpoint, so the netlist's `.tran` output interval need not equal
`SampleTime`. The `.tran` stop time must still cover the full PySysLink
simulation duration. For example, a model running 2000 steps of `0.01` seconds
needs a stop time of at least 20 seconds; ngspice will end the analysis when
its `.tran` stop time is reached. At each breakpoint, the transient data
callback waits while PySysLink supplies the next inputs, then continues the
same ngspice transient run without restarting the analysis.

Netlist paths must not contain whitespace or quotes because of ngspice's
interactive `source` command syntax.

The ngspice shared library maintains process-global state, so this initial
implementation permits one active `Ngspice` block per process. Netlists must
not contain a `.control` section. ngspice handles the transient solver; the
block synchronizes each discrete PySysLink call with the corresponding
transient output point. Circuit rewinds, skipped sample steps, AC/DC analyses,
and multiple concurrent ngspice blocks are not supported.