.. _quickstart:

快速开始
==========
:full-width:

使用NPU-SMI十分简单：

1. 准备 对应 Config 文件，在llm/test/目录下面。
2. 使用命令行启动模型服务。

运行
------------

.. code-block:: console

    cd build
    ./npusim --config-file ../llm/test/config_pd_sim.json

其中 ``config_pd_sim.json`` 为一个配置文件，你可以根据你的需求进行修改。

.. note::
    在继续这个教程之前，请确保你完成了 :ref:`安装指南 <installation>`, 并确保配置了相关的环境变量。




.. _configuration-fields:

配置字段说明
--------------

以下是对配置文件中各个字段的详细说明：

通用控制参数
~~~~~~~~~~~~~

random : bool, optional
  默认为 false，是否启用工作核随机排列，会反应在生成的数据流图上。

pipeline : int, optional
  默认为 1。如果该值大于 1，则开启 pipeline 模式，具体表现为 memInterface 会将 source 字段中的 start data 复制对应次数连续发送给工作核，在 memInterface 接收到 pipeline 对应数量的 DONE 信号后，程序结束。
  简单的来说，pipeline 的次数就是 下发的 input 请求的次数。

sequential : bool, optional
  默认为 false。如果为 true，则开启 sequential 模式，具体表现为 memInterface 会在接收到每一个 DONE 信号之后，依次发送 source 字段数组下的所有 start data，每次只发送数组中的一个元素，在发送完毕并接收到最后的 DONE 信号之后，程序结束。

  .. note::
     ``sequential`` 和 ``pipeline`` 模式不能同时启用。目前 ``sequential`` 仅用于 pd 阶段的模拟。
  .. code-block:: json

    "source": [
      {
        "dest": 0,
        "size": "BTC"
      },
      {
        "dest": 3,
        "size": "C",
        "loop": "L"
      }
    ]

  上面的配置表示 1次0，L次3 模拟 ``prefill`` 和 ``decoding`` 阶段的 input 下发

vars : dict
  记录数值的键值对，在下方的配置中出现的所有字符串可以在这里转换成对应的数字。

  
  .. note::
     如果配置 sram 的地址的话，需要 1024 BYTE 对齐，除以  1024 取整。


source : list of dicts
  一个数组，记录了所有 start data 的相关信息。如果不为 sequential 模式，则在程序开始时、发送完所有的 prepare data 之后，memInterface 会一次性发送所有的 start data。

  每个字典包含以下字段：

  - dest : int
      start data 发送的目的地核编号。
  - size : string
      start data 的大小，在 ``vars`` 中查找对应值。
  - loop : int, optional
      默认为 1。需要循环发送本 start data 的次数，**仅在 sequential 模式下使用**。

chip : list of dicts
  记录拓扑配置，虽然是数组但目前仅使用数组的第一个元素。

  每个字典包含以下字段：

  - GridX : int
      X 维度计算核的个数。
  - GridY : int
      Y 维度计算核的个数（目前需要强制等同于 GridX）。
  - cores : list of dicts
      计算核相关配置，每一个数组元素代表一个核。

core 配置
~~~~~~~~~~~~~~~~~

每个 core 包含以下字段：

- id : int
    计算核 ID。

- prim_prefill : bool, optional
    默认为 false。该核是否需要支持无限循环执行，在 pipeline 模式下需要开启。

- prim_copy : int, optional
    默认为 -1（不开启）。该核是否需要完全复制另一个核 worklist 中的原语。但需注意如果要复制的话，还是需要在自己的 worklist 中注明对应的 cast、recv_cnt 和 recv_tag。

- worklist : list of dicts
    按照顺序指示计算核需要完成的工作。

    每个 worklist 元素包含以下字段：

    - recv_cnt : int
        在执行这个 worklist 数组元素的原语之前，需要接收到多少个对应 tag 的 SEND_DRAM 原语的 end packet。

    - recv_tag : int, optional
        默认值为此计算核 id。被此 worklist 数组元素所接受的 SEND msg 的 tag。不是此 tag 的消息不会被接收。
        
        .. note::
           在配置文件时，需要注意每一个核的第一个 worklist 数组元素的 tag 必须与此计算核的 id 相同。且在后续的 worklist 元素中，tag 必须与此计算核的 id 不同，推荐在原 id 基础上增加一个较大的值。

    - cast : list of dicts
        在此 worklist 元素的所有原语完成之后，需要将结果发送到哪些核。

        每个 cast 元素包含以下字段：

        - dest : int
            目标核 ID。
        - addr : int
            目标核 DRAM 偏移量。

    - prims : list of dicts
        此 worklist 元素需要完成的所有 comp 原语。

        每个 prim 元素包含以下字段：

        - type : string
            原语类型（需填写指定字符串）。

        - vars : string or int
            vars 处填写原语需要的参数名，值可以用 string 在 ``vars`` 字段查找，也可以填写数字。

        - sram_address : dict
            此原语在 SRAM 中存储相关。

            - indata : string
                此原语的输入位于 SRAM 的什么标签处。如果需要从 DRAM 获取，则必须先写 "dram_label"，随后在一个空格后加上从 DRAM 读取出数据后存放在 SRAM 中的标签名。如果原语会有几部分的输入，则统一用一个空格隔开。

                .. note::
                   - 对于上一个核路由传进来的输入数据（保存在 SRAM 上），则在 ``sram_address`` 中用 ``input_label`` 表示。
                   - 一般来说，算子的输入张量，用完即可清除，但是对于类似 residual 算子，一个输入张量可能会被后续张量使用，需要在 ``input_label`` 前加上 ``_input_label``。

            - outdata : string
                此原语的输出会保存在 SRAM 的什么标签处。

        - dram_address : dict
            此原语在 DRAM 中存储相关。

            - input : string or int, optional
                默认为 0。此原语输入在 DRAM 中的位置。

            - data : string or int, optional
                默认为 0。此原语数据、权重在 DRAM 中的位置，如果为 -1，则表示此原语不需要权重的数据。

            - out : string or int, optional
                默认为 0。此原语输出在 DRAM 中的位置。