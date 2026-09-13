/* -*- c++ -*- */
/*
 * Copyright © 2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

class elk_fs_visitor;

namespace elk {
   class vec4_visitor;

   /**
    * Various estimates of the performance of a shader based on static
    * analysis.
    */
   struct performance {
      performance(const elk_fs_visitor *v);
      performance(const vec4_visitor *v);
      ~performance();

      analysis_dependency_class
      dependency_class() const
      {
         return (DEPENDENCY_INSTRUCTIONS |
                 DEPENDENCY_BLOCKS);
      }

      bool
      validate(const elk_backend_shader *) const
      {
         return true;
      }

      /**
       * Array containing estimates of the runtime of each basic block of the
       * program in cycle units.
       */
      unsigned *block_latency;

      /**
       * Estimate of the runtime of the whole program in cycle units assuming
       * uncontended execution.
       */
      unsigned latency;

      /**
       * Estimate of the throughput of the whole program in
       * invocations-per-cycle units.
       *
       * Note that this might be lower than the ratio between the dispatch
       * width of the program and its latency estimate in cases where
       * performance doesn't scale without limits as a function of its thread
       * parallelism, e.g. due to the existence of a bottleneck in a shared
       * function.
       */
      float throughput;

   private:
      performance(const performance &perf);
      performance &
      operator=(performance u);
   };
}
