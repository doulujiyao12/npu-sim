.. _installation:

安装指南
============

NPU-SIM 是一个用SystemC库写的轻量级众核仿真器，可灵活适配多种众核模式（包括SIMD以及DataFlow）。

环境要求
------------

* 操作系统: Linux
* SystemC: 2.3.3
* Cmake: 3.31.3
* G++: 9.4.0


.. _build_from_source:

使用源码安装
----------------

你也可以使用源码安装NPU-SIM：

安装SytemC
~~~~~~~~~~~~~
.. code-block:: console

    wget https://github.com/accellera-official/systemc/archive/refs/tags/2.3.3.tar.gz
    tar -zxvf 2.3.3.tar.gz
    cd systemc-2.3.3/
    mkdir tmp && cd tmp
    ../configure --prefix=/path/to/install/systemc-2.3.3 CXXFLAGS="-std=c++17"
    sudo make -j8
    make install
    # 如果想用GDB对SystemC进行调试，可以加上下面的参数
    # ../configure --prefix=/path/to/install/systemc-2.3.3_debug --enable-debug  CXXFLAGS="-std=c++17"

安装CMake 3.31.3
~~~~~~~~~~~~~~~~~
.. code-block:: console

    #https://cmake.org/download/ Cmake 官网上下载源码或者对应的二进制文件
    wget https://cmake.org/files/v3.31/cmake-3.31.3-linux-x86_64.tar.gz
    tar -zxvf cmake-3.31.3-linux-x86_64.tar.gz 



安装JSON 库
~~~~~~~~~~~~~
.. code-block:: console

    git clone --branch=v3.11.3 --single-branch --depth=1 https://github.com/nlohmann/json.git
    cd json
    mkdir build && cd build
    cmake ..
    make
    sudo make install

安装多媒体库
~~~~~~~~~~~~~
.. code-block:: console

    # 安装 SFML
    sudo apt-get install libsfml-dev # 可能依赖 libsfml-audio2.3v5=2.3.2+dfsg-1  libopenal1 libopenal-data=1:1.16.0-3

    # 安装 CAIRO
    sudo apt install libcairo2-dev

    # 安装 X11（服务器环境可能需要）
    sudo apt install xorg

    # 安装字体（源文件已包含必要的 ttf 文件）
    sudo apt install ttf-mscorefonts-installer  # 需要在弹出界面选择 OK

配置环境路径
~~~~~~~~~~~~~
.. code-block:: console

    export SYSTEMC_HOME=/path/to/install/systemc-2.3.3/
    export LD_LIBRARY_PATH=/path/to/install/systemc-2.3.3/lib-linux64/:$LD_LIBRARY_PATHs
    export CMAKE_HOME=/path/to/install/cmake-3.31.3-linux-x86_64/bin/
    export PATH=$CMAKE_HOME:$PATH

下载编译
~~~~~~~~~~~~~
.. code-block:: console

    git clone https://gitee.com/doulujiyao/npu-sim
    cd npu-sim
    mkdir build && cd build
    cmake -DBUILD_DEBUG_TARGETS=OFF ..
    # 开启 调试模式
    cmake -DBUILD_DEBUG_TARGETS=ON ..

.. note::

    SytemC 仅测试了2.3.3版本，其他版本可能会出现不兼容的情况。

