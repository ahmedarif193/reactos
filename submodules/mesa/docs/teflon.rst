TensorFlow Lite delegate
========================

Mesa contains a TensorFlow Lite delegate that can make use of NPUs to accelerate ML inference. It is implemented in the form of a *external delegate*, a shared library that the TensorFlow Lite runtime can load at startup. See https://www.tensorflow.org/api_docs/python/tf/lite/experimental/load_delegate.

.. list-table:: Supported acceleration hardware
   :header-rows: 1

   * - Gallium driver
     - NPU supported
     - Hardware tested
   * - Etnaviv
     - ``VeriSilicon VIPNano-QI.7120``
     - ``Amlogic A311D on Libre Computer AML-A311D-CC Alta and Khadas VIM3``
   * - Etnaviv
     - ``VeriSilicon VIPNano-SI+.8002``
     - ``NXP iMX8M Plus on Toradex Verdin SoM``
   * - Rocket
     - ``RK3588 NPU``
     - ``PINE64 QuartzPro64``

.. list-table:: Tested models
   :header-rows: 1

   * - Model name
     - Data type
     - Link (may be outdated)
     - Status
     - Inference speed on AML-A311D-CC Alta
     - Inference speed on Verdin iMX8M Plus
     - Inference speed on QuartzPro64
   * - MobileNet V1
     - UINT8
     - http://download.tensorflow.org/models/mobilenet_v1_2018_08_02/mobilenet_v1_1.0_224_quant.tgz
     - Fully supported
     - ~6.6 ms
     - ~7.9 ms
     - ~18 ms
   * - MobileNet V2
     - UINT8
     - https://storage.googleapis.com/mobilenet_v2/checkpoints/quantized_v2_224_100.tgz
     - Fully supported
     - ~6.9 ms
     - ~8.0 ms
     - ~21 ms
   * - SSDLite MobileDet
     - UINT8
     - https://raw.githubusercontent.com/google-coral/test_data/master/ssdlite_mobiledet_coco_qat_postprocess.tflite
     - Fully supported
     - ~24.8 ms
     - ~24.4 ms
     - ~48 ms

Build
-----

Build Mesa as usual, with the -Dteflon=true argument. Make sure at least one of etnaviv or rocket gallium drivers is enabled, as Teflon only works with these drivers.

Example instructions:

.. code-block:: console

   # Install build dependencies
   ~ # apt-get -y build-dep mesa
   ~ # apt-get -y install git cmake

   # Download sources
   ~ $ git clone https://gitlab.freedesktop.org/mesa/mesa.git

   # Build Mesa
   ~ $ cd mesa
   mesa $ meson setup build -Dgallium-drivers=etnaviv,rocket -Dvulkan-drivers= -Dteflon=true
   mesa $ meson compile -C build

Install runtime dependencies
----------------------------

Your board should have booted into a mainline 6.7 (6.8 for the i.MX8MP) or greater kernel.

.. code-block:: console

   # Install Python 3.10 and dependencies (as root)
   ~ # echo deb-src http://deb.debian.org/debian testing main >> /etc/apt/sources.list
   ~ # echo deb http://deb.debian.org/debian unstable main >> /etc/apt/sources.list
   ~ # echo 'APT::Default-Release "testing";' >> /etc/apt/apt.conf
   ~ # apt-get update
   ~ # apt-get -y install python3.10 python3-pytest python3-exceptiongroup

   # Install TensorFlow Lite Python package (as non-root)
   ~ $ python3.10 -m pip install --break-system-packages tflite-runtime==2.13.0

   # For the classification.py script mentioned below, you will need PIL
   ~ $ python3.10 -m pip install --break-system-packages pillow

Do some inference with MobileNetV1
----------------------------------

Run the above for a quick way of checking that the setup is correct and the NPU is accelerating the inference. It assumes you have followed the steps above so Python 3.10 and dependencies have been installed, and assumes that Mesa was built to the ``./build`` directory.

You can use any image that prominently features one of the objects in the ``src/gallium/frontends/teflon/tests/labels_mobilenet_quant_v1_224.txt`` file. You can use an [image of Grace Hopper](https://raw.githubusercontent.com/tensorflow/tensorflow/master/tensorflow/lite/examples/label_image/testdata/grace_hopper.bmp) as a running example.

This example script has been based from the code in https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/examples/python.

.. code-block:: console

   ~ $ cd mesa/
   mesa $ TEFLON_DEBUG=verbose ETNA_MESA_DEBUG=ml_msgs python3.10 src/gallium/frontends/teflon/tests/classification.py \
          -i ~/tensorflow/assets/grace_hopper.bmp \
          -m src/gallium/targets/teflon/tests/models/mobilenetv1/mobilenet_v1_1_224_quant.tflite \
          -l src/gallium/frontends/teflon/tests/labels_mobilenet_quant_v1_224.txt \
          -e build/src/gallium/targets/teflon/libteflon.so

   Loading external delegate from build/src/gallium/targets/teflon/libteflon.so with args: {}
   Teflon delegate: loaded etnaviv driver
   idx type            ver support     inputs
   ================================================================================================
     0 CONV            v1  supported   in: 88(u8) 8(u8) 6(i32) out: 7(u8)
     1 DWCONV          v1  supported   in: 7(u8) 35(u8) 34(i32) out: 33(u8)
     2 CONV            v1  supported   in: 33(u8) 38(u8) 36(i32) out: 37(u8)
     3 DWCONV          v1  supported   in: 37(u8) 41(u8) 40(i32) out: 39(u8)
     4 CONV            v1  supported   in: 39(u8) 44(u8) 42(i32) out: 43(u8)
     5 DWCONV          v1  supported   in: 43(u8) 47(u8) 46(i32) out: 45(u8)
     6 CONV            v1  supported   in: 45(u8) 50(u8) 48(i32) out: 49(u8)
     7 DWCONV          v1  supported   in: 49(u8) 53(u8) 52(i32) out: 51(u8)
     8 CONV            v1  supported   in: 51(u8) 56(u8) 54(i32) out: 55(u8)
     9 DWCONV          v1  supported   in: 55(u8) 59(u8) 58(i32) out: 57(u8)
   ...

   teflon: compiling graph: 89 tensors 27 operations
   idx scale     zp has_data size
   =======================================
     0 0.023528   0 no       1x1x1x1024
     1 0.166099  42 no       1x1x1x1001
     2 0.000117   0 yes      1x1x1x1001
     3 0.004987  4a yes      1001x1x1x1024
     4 0.166099  42 no       1x1x1x1001
     5 0.000000   0 yes      1x1x1x2
     6 0.000171   0 yes      1x1x1x32
     7 0.023528   0 no       1x112x112x32
     8 0.021827  97 yes      32x3x3x3
     9 0.023528   0 no       1x14x14x512
   ...

   idx type            inputs               outputs
   ==========================================================================
     0 CONV            88,8,6               7
     1 DWCONV          7,35,34              33
     2 CONV            33,38,36             37
     3 DWCONV          37,41,40             39
     4 CONV            39,44,42             43
     5 DWCONV          43,47,46             45
     6 CONV            45,50,48             49
     7 DWCONV          49,53,52             51
     8 CONV            51,56,54             55
     9 DWCONV          55,59,58             57
    10 CONV            57,62,60             61
    11 DWCONV          61,65,64             63
    12 CONV            63,68,66             67
    13 DWCONV          67,71,70             69
    14 CONV            69,74,72             73
    15 DWCONV          73,77,76             75
    16 CONV            75,80,78             79
    17 DWCONV          79,83,82             81
    18 CONV            81,86,84             85
    19 DWCONV          85,11,10             9
    20 CONV            9,14,12              13
    21 DWCONV          13,17,16             15
    22 CONV            15,20,18             19
    23 DWCONV          19,23,22             21
    24 CONV            21,26,24             25
    25 DWCONV          25,29,28             27
    26 CONV            27,32,30             31

   teflon: compiled graph, took 1911 ms

   ...
   teflon: compiled graph, took 602 ms
   teflon: invoked graph, took 7 ms
   teflon: invoked graph, took 1 ms
   teflon: invoked graph, took 7 ms
   teflon: invoked graph, took 0 ms
   teflon: invoked graph, took 7 ms
   teflon: invoked graph, took 1 ms
   teflon: invoked graph, took 7 ms
   teflon: invoked graph, took 0 ms
   teflon: invoked graph, took 7 ms
   teflon: invoked graph, took 0 ms
   0.870588: military uniform
   0.031373: Windsor tie
   0.011765: mortarboard
   0.007843: bow tie
   0.007843: bulletproof vest
   time: 7.549ms
