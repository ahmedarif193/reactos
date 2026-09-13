Performance Tips
================

Intel GPUs
----------

#. Keep the system updated with the latest kernel and Mesa versions.
#. Ensure SoC firmware is up-to-date. These firmware updates currently
   require installing the Windows graphics driver; firmware updates
   via `fwupd` are in progress.
#. Use Wayland where possible, as it supports additional modifiers for
   better performance.
#. For MTL and newer integrated GPUs, disable VT-d if virtualization is
   not needed.
#. For discrete GPUs:

   #. Enable `ReBAR`_
   #. For workloads that keep the GPU busy (e.g. 3D videogames), 
      minimize idle power consumption by enabling `ASPM`_ powersave mode.
   #. For "bursty", latency-sensitive workloads (e.g. AI inference),
      enable ASPM performance mode.
   #. Enable the 'performance' `cpufreq`_ governor.

.. _ReBAR: https://www.intel.com/content/www/us/en/support/articles/000090831/graphics.html
.. _ASPM: https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/6/html/power_management_guide/aspm
.. _cpufreq: https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/6/html/performance_tuning_guide/s-cpu-cpufreq
