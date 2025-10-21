import sys

def process_file(filepath):
    with open(filepath, "r", encoding="utf-8") as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        if lines[i].strip() == '"type": "parse_input",':
            target_idx = i + 3
            if target_idx < len(lines):
                # 保留缩进风格
                indent = len(lines[target_idx]) - len(lines[target_idx].lstrip())
                lines[target_idx] = " " * indent + '"indata": "input_label",\n'
        i += 1

    with open(filepath, "w", encoding="utf-8") as f:
        f.writelines(lines)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"用法: python {sys.argv[0]} 文件路径")
    else:
        process_file(sys.argv[1])
