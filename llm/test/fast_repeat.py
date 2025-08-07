import sys
import json

def replace_star_and_generate(input_path, start, end):
    with open(input_path, 'r', encoding='utf-8') as f:
        original_json = json.load(f)

    result = []

    for i in range(start, end + 1):
        # 将 JSON 中所有的字符串值中的 * 替换为当前数字
        replaced = replace_in_json(original_json, str(i), str(i+1))
        result.append(replaced)

    return result

def replace_in_json(obj, replacement1, replacement2):
    if isinstance(obj, dict):
        return {k: replace_in_json(v, replacement1, replacement2) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [replace_in_json(elem, replacement1, replacement2) for elem in obj]
    elif isinstance(obj, str):
        return obj.replace('*', replacement1).replace('$', replacement2)
    else:
        return obj

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("用法: python script.py <json路径> <起始数字> <结束数字>")
        sys.exit(1)

    input_path = sys.argv[1]
    start = int(sys.argv[2])
    end = int(sys.argv[3])

    output = replace_star_and_generate(input_path, start, end)

    output_path = "output.json"
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(output, f, indent=2, ensure_ascii=False)

    print(f"已生成文件: {output_path}")
