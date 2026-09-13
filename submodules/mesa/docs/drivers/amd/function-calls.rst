:orphan:

.. _aco-fn-calls:

Function call support in RADV/ACO
=================================

ACO supports function calls inside shaders - given a function signature and ABI, shaders can call
an arbitrary function, even via only a function pointer (i.e. with an unknown function definition).

This function call support is useful for implementing ray tracing pipelines (by representing individual RT shaders
as callable functions), but it also has potential use cases in GPGPU/Compute workloads.

This page serves to document the concepts involved in implementing function calls as well as an overview of the
implementation components.

Function call representation
----------------------------

In NIR, function calls are represented by a `nir_call_instr`. The instruction takes a `nir_function` representing
the function being called, as well as SSA defs for each call parameter.
NIR can also represent "indirect calls", i.e. calls where the function being called is
unknown - instead, the instruction takes an SSA def containing a function pointer to the callee. In this case, the
`nir_function` only serves to provide information about the function signature, i.e. how many and which parameters
the function takes.

Call instructions do not have return values - instead, return values are represented by so-called "return parameters".
Instead of an SSA value, these parameters are derefs, and the return value is written into the deref when the callee
returns. Return parameters can double as input parameters, too - the callee can read the previous value of the deref
before (potentially) overwriting it with a new value.

ACO's representation of function calls follows this very closely. Calls are described by the `p_call` pseudo-instruction.
The operands to this instruction are a function pointer (i.e. the address of the callee), followed by the call
parameters. Return parameters are handled differently, though: While the initial value of the return parameter is passed
as an operand, the call instruction produces new definitions that refer to the SSA values of the return parameters after
the function call returns. There is a special NIR intrinsic ``load_return_param_amd`` that can be used to access these
new definitions when lowering return parameter derefs to SSA form.

.. _div-calls:

Divergent calls
---------------

On CPUs, a call instruction will only ever jump to a single address. However, GPUs are SIMT, and the value of a function
pointer may be divergent, i.e. different threads try calling different functions within the same call instruction. AMD
hardware executes one instruction for all threads in lockstep, so the multiple callees have to be executed one after
the other.

This is handled by RADV in ``radv_nir_lower_call_abi``. In addition to the (non-divergent) function pointer to jump to,
``radv_nir_lower_call_abi`` prepends another parameter representing the (potentially divergent) function pointer for all
lanes. For callable functions, ``radv_nir_lower_call_abi`` wraps the function body in a condition that verifies that the
current thread's (divergent) pointer matches the (non-divergent) pointer that is currently being executed. This serves
to "mask off" all threads that wanted to jump to a different function than what is currently executing. At the very end,
``radv_nir_lower_call_abi`` inserts some code deciding whether to jump to the next callee or to return.

.. _stack:

Stack
-----

Supporting arbitrary function calls also means supporting recursion, and recursive functions need a stack.
AMD hardware provides instructions for accessing a per-thread scratch memory area in VRAM, and ACO uses this per-thread
scratch memory to set up its stack.

The stack frame for a function consists of all scratch memory allocated for this function in NIR, as well as space to
spill VGPRs if that is required. ACO adds a stack pointer as a parameter to every function - this stack pointer is added
to the offset inside the scratch space for all scratch loads/stores to make sure they don't overwrite stack frames of
caller functions.

ACO's call instructions take two stack-related operands: The current (caller) stack pointer and the caller's stack size.
When converting the call instruction to hardware instructions, ACO will add the caller stack size to the stack pointer
for the duration of the call (and subtract it again afterwards). This allows us to re-use the same stack pointer after
the call.

Implicit/System Parameters
--------------------------

In addition to parameters defined by the function signature, both RADV and ACO will insert additional parameters while
lowering calls. This is an overview of which lowering passes add which parameters.

Parameters added by ``radv_nir_lower_call_abi`` (see :ref:`Divergent calls <div-calls>`):
- "Uniform"/Non-divergent callee pointer
- Divergent function pointer

Parameters added by ACO: (see :ref:`Stack <stack>`)
- Stack pointer (uniform)

ABI Definition
--------------

The ABI (Application Binary Interface) defines specifics about the interaction between the function caller and the
callee (e.g. assignment of registers to parameters or register preservation). In ACO, the primary purpose of the ABI is
to define which register ranges are "preserved" (i.e. never overwritten by the callee) or "clobbered" (i.e. potentially
overwritten by the callee).

The caller can use preserved register ranges to store temporaries that are live across a call, and the callee can use
clobbered register ranges to store its own temporaries. If the callee wants to use registers from a preserved range,
then it needs to back up the value contained in the preserved register beforehand, and restore it when it's done using
the preserved register. Similarly, if there are not enough preserved registers for the caller to store all its
temporaries, the caller will need to spill excess temporaries to the stack.

ACO has to cater to different needs when defining ABIs: On one side, ray tracing traversal shaders demand to free up
the entire register file for the callee (Ray traversal is a really hot loop, so we don't want to spill anything at all).
Besides some parameters like the invocation ID, these shaders should be able to overwrite almost anything. On the other
side, RT traversal shaders should not be required to free up the register file when calling any-hit/intersection shaders
as this would also cause spilling during traversal. GPGPU compute workloads could fall anywhere between these extremes,
so a middle-ground solution is desirable for these.

ACO's way of defining an ABI divides the register file into "blocks" (``struct aco::ABI::RegisterBlock``). Each block
consists of a fixed number of preserved and clobbered registers, and a boolean determining whether the preserved or
clobbered registers come first in the block. Preserved and clobbered register ranges are defined by
repeating these blocks for as long as there are unassigned registers.

Some examples of preserved/clobbered register ranges using this approach::

   For all examples, there are 108 SGPRs and 128 VGPRs to assign.

   RegisterBlock:
      clobbered_size: {16 sgpr, 16 vgpr}
      preserved_size: {16 sgpr, 16 vgpr}
      clobbered_first: false
   results in:
      v0-v15:    preserved
      v16-v31:   clobbered
      v32-v47:   preserved
      v48-v63:   clobbered
      v64-v79:   preserved
      v80-v95:   clobbered
      v96-v111:  preserved
      v112-v127: clobbered

      s0-s15:   preserved
      s16-s31:  clobbered
      s32-s47:  preserved
      s48-s63:  clobbered
      s64-s79:  preserved
      s80-s95:  clobbered
      s96-s108: preserved

   RegisterBlock:
      clobbered_size: {128 sgpr, 256 vgpr}
      preserved_size: {80 sgpr, 80 vgpr}
      clobbered_first: false
   results in:
      v0-v79:   preserved
      v80-v127: clobbered

      s0-s79:   preserved
      s80-s108: clobbered

An alternating preserved-clobbered-preserved pattern is useful for generic compute workloads, because the ratio of
preserved to clobbered registers is roughly the same, no matter how many registers are used by the shaders.

The latter example where the lower part of the register file is preserved and only some registers high up in the
register file are clobbered is suitable for any-hit/intersection shaders - traversal shader temporaries can live in the
preserved part low in the register file.

This block assignment is optional - if no ``RegisterBlock`` is given, the ABI defines the entire register range as
clobbered-by-default, although parameters that are not marked as clobbered via ``ACO_NIR_PARAM_ATTRIB_DISCARDABLE``
will continue being preserved.

Parameter Register Assignment
-----------------------------

If a ``RegisterBlock`` defines preserved and clobbered ranges, then parameters are assigned registers from either range
depending on ``ACO_NIR_PARAM_ATTRIB_DISCARDABLE`` - if parameters are marked as clobbered with this attribute, then they
are assigned a register in a clobbered range, otherwise they are assigned in a register in a preserved range. The order
of the parameters in the register file is not necessarily the same order as in the function signature - they may get
reordered if it's beneficial to fill gaps or for alignment.

If there is no ``RegisterBlock``, then registers will be assigned based on alignment only.

If there is no more space for a parameter in any of its corresponding register ranges, it will be moved to the stack.