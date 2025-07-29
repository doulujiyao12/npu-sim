import sys
import re
import json

def main():
    if len(sys.argv) < 3:
        print("Usage: python replace_json_placeholders.py <input.json> <b0> <b1> ...")
        return

    json_path = sys.argv[1]
    b_values = list(map(int, sys.argv[2:]))
    mapping = {str(i): str(b) for i, b in enumerate(b_values)}

    with open(json_path, "r", encoding="utf-8") as f:
        content = f.read()

    # 替换所有 *A* 为对应的 B
    def replacer(match):
        a = match.group(1)
        if a in mapping:
            return mapping[a]
        else:
            print(f"Warning: No replacement found for *{a}*")
            return match.group(0)

    new_content = re.sub(r"\*(\d+)\*", replacer, content)

    # 可选：验证替换结果是否为合法 JSON
    try:
        json.loads(new_content)
    except json.JSONDecodeError as e:
        print("Error: Resulting content is not valid JSON after replacement.")
        print(e)
        return

    # 写回文件
    with open(json_path, "w", encoding="utf-8") as f:
        f.write(new_content)

    print("Replacement completed successfully.")

if __name__ == "__main__":
    main()
