Shading Language
================

This page describes some tools for working with Mesa's support for the
`OpenGL Shading Language <https://wikis.khronos.org/opengl/OpenGL_Shading_Language>`__.

.. _envvars:

Environment Variables
---------------------

The **MESA_GLSL** environment variable can be set to a comma-separated
list of keywords to control some aspects of the GLSL compiler and shader
execution. These are generally used for debugging.

-  **dump** - print GLSL shader code, IR, and NIR to stdout at link time
-  **source** - print GLSL shader code to stdout at link time
-  **log** - log all GLSL shaders to files. The filenames will be
   "shader_X.vert" or "shader_X.frag" where X the shader ID.
-  **cache_info** - print debug information about shader cache
-  **cache_fb** - force cached shaders to be ignored and do a full
   recompile via the fallback path
-  **uniform** - print message to stdout when glUniform is called
-  **nopvert** - force vertex shaders to be a simple shader that just
   transforms the vertex position with ftransform() and passes through
   the color and texcoord[0] attributes.
-  **nopfrag** - force fragment shader to be a simple shader that passes
   through the color attribute.
-  **useprog** - log glUseProgram calls to stderr
-  **errors** - GLSL compilation and link errors will be reported to
   stderr.

Example: export MESA_GLSL=dump,nopt

.. _replacement:

Experimenting with Shader Replacements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Shaders can be dumped and replaced on runtime for debugging purposes.
This is controlled via following environment variables:

-  **MESA_SHADER_DUMP_PATH** - path where shader sources are dumped
-  **MESA_SHADER_READ_PATH** - path where replacement shaders are read

Note, path set must exist before running for dumping or replacing to
work. When both are set, these paths should be different so the dumped
shaders do not clobber the replacement shaders. Also, the filenames of
the replacement shaders should match the filenames of the corresponding
dumped shaders.

.. _capture:

Capturing Shaders
~~~~~~~~~~~~~~~~~

Setting **MESA_SHADER_CAPTURE_PATH** to a directory will cause the
compiler to write ``.shader_test`` files for use with
`shader-db <https://gitlab.freedesktop.org/mesa/shader-db>`__, a tool
which compiler developers can use to gather statistics about shaders
(instructions, cycles, memory accesses, and so on).

Notably, this captures linked GLSL shaders - with all stages together -
as well as ARB programs.

Implementation Notes
--------------------

-  Shading language programs are compiled first into an AST-related high level
   IR, then into the NIR common shading langauge IR for optimization and
   transformation before going to a backend driver's shader compiler.
-  All function calls are inlined.

Stand-alone GLSL Compiler
-------------------------

The stand-alone GLSL compiler program can be used to compile GLSL
shaders into GLSL IR code.

This tool is useful for:

-  Inspecting GLSL frontend behavior to gain insight into compilation
-  Debugging the GLSL compiler itself

After building Mesa with the ``-Dtools=glsl`` meson option, the compiler will be
installed as the binary ``glsl_compiler``.

Here's an example of using the compiler to compile a vertex shader.

.. code-block:: sh

       src/compiler/glsl/glsl_compiler --version XXX --dump-ast myshader.vert

Options include

..  option:: --dump-ast

   dump source syntax tree

.. option:: --dump-hir

   dump high-level IR code

.. option:: --dump-lir

   dump low-level IR code

.. option:: --link

   link shaders

.. option:: --just-log

   display only shader / linker info if exist, without
   any header or separator

.. option:: --version

   [Mandatory] define the GLSL version to use

Compiler Implementation
-----------------------

The source code for Mesa's shading language compiler is in the
``src/compiler/glsl/`` directory.
