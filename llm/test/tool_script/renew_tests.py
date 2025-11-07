import subprocess, glob, os
from collections import deque

workloads = glob.glob("../llm/test/workload_config/paper/fig11/M*.json")
hardware = glob.glob("../llm/test/hardware_config/default_hw.json")
sim_cfg = "../llm/test/simulation_config/default_spec.json"

output_file = "result_summary.txt"

with open(output_file, "w") as fout:
    for wl in workloads:
        for hw in hardware:
            print(f"Running {wl}  x  {hw}")

            # 存最后三行
            last_lines = deque(maxlen=3)

            # 流式读取输出，不缓存全部内容
            p = subprocess.Popen(
                ["./npusim", "--workload-config", wl, "--hardware-config", hw, "--simulation-config", sim_cfg],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1 # 行缓冲
            )

            for line in p.stdout:
                print(line, end="")   # 正常打印出来，不影响npusim输出体验
                last_lines.append(line.strip())

            p.wait()

            if len(last_lines) >= 3:
                useful = list(last_lines)[-3]
            else:
                useful = list(last_lines)[-1]

            fout.write(f"{os.path.basename(wl)}, {os.path.basename(hw)}, {useful}\n")
