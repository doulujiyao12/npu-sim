.. _run_npu_sim:

运行 NPU-SIM
============

使用以下命令运行 NPU-SIM：

.. code-block:: console

    ${NPU_SIM_PATH} \
        --workload-config ${WORKLOAD_CONFIG_PATH} \
        --simulation-config ${SIMULATION_CONFIG_PATH} \
        --hardware-config ${HARDWARE_CONFIG_PATH} \
        --mapping-config ${MAPPING_CONFIG_PATH}

命令行参数说明
--------------

以下是各个命令行参数的说明：

- **--workload-config**  
  指定工作负载配置文件路径。  

  详情请参阅：:doc:`workload_config_detail`

- **--simulation-config**  
  指定仿真配置文件路径。  

  详情请参阅：:doc:`simulation_config_detail`

- **--hardware-config**  
  指定硬件配置文件路径。  

  详情请参阅：:doc:`hardware_config_detail`

- **--mapping-config**  
  指定映射配置文件路径。  

  详情请参阅：:doc:`mapping_config_detail`


附属页面
--------

.. toctree::
   :maxdepth: 1

   workload_config_detail
   simulation_config_detail
   hardware_config_detail
   mapping_config_detail
