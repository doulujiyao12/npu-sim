import matplotlib.pyplot as plt
import pandas as pd

# 原始数据
data = [
    ("mn", 256, 768, 32107136),
    ("mnk", 256, 768, 17431558),
    ("k", 256, 768, 17349746),
    ("mn", 512, 768, 34361210),
    ("mnk", 512, 768, 21795240),
    ("k", 512, 768, 34361210),
    ("mn", 1024, 768, 33344870),
    ("mnk", 1024, 768, 30934046),
    ("k", 1024, 768, 68842370),
    ("mn", 256, 1024, 54446848),
    ("mnk", 256, 1024, 29192902),
    ("k", 256, 1024, 23266386),
    ("mn", 512, 1024, 60351096),
    ("mnk", 512, 1024, 35148388),
    ("k", 512, 1024, 46013710),
    ("mn", 1024, 1024, 74799752),
    ("mnk", 1024, 1024, 47640544),
    ("k", 1024, 1024, 92144406),
]

# 转换为 DataFrame
df = pd.DataFrame(data, columns=["TP_Mode", "Seq_Len", "Hidden_Size", "Latency_ns"])

# 创建一个新列作为横轴标签
df["Seq_Hidden"] = list(zip(df["Seq_Len"], df["Hidden_Size"]))
df = df.sort_values(by=["Seq_Len", "Hidden_Size"])

# 绘图
plt.figure(figsize=(12, 6))
for mode in df["TP_Mode"].unique():
    subset = df[df["TP_Mode"] == mode]
    x_labels = [f"{s}x{h}" for s, h in subset["Seq_Hidden"]]
    plt.plot(x_labels, subset["Latency_ns"], marker='o', label=f"TP {mode}")

plt.xticks(rotation=45)
plt.xlabel("Sequence Length x Hidden Size")
plt.ylabel("Latency (ns)")
plt.title("Latency by TP Mode vs Input Size (Seq x Hidden)")
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()
