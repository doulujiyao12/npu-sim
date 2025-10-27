import json
import sys
import copy
import re

# recv_tag 替换映射
RECV_TAG_MAP = {
    0: 105, 1: 90, 2: 91, 3: 92,
    4: 93, 5: 94, 6: 95, 7: 96,
    8: 97, 9: 98, 10: 99, 11: 100,
    12: 101, 13: 102, 14: 103, 15: 104
}

# cast tag/dest 映射
CAST_RULES = {
    0: (90, lambda k: k + 1),
    1: (91, lambda k: k + 1),
    2: (92, lambda k: k + 1),
    3: (93, lambda k: k + 1),
    4: (94, lambda k:k + 1),
    5: (95, lambda k:k + 1),
    6: (96, lambda k: k + 1),
    7: (97, lambda k: k + 1),
    8: (98, lambda k: k + 1),
    9: (99, lambda k: k + 1),
    10: (100, lambda k: k + 1),
    11: (101, lambda k: k + 1),
    12: (102, lambda k: k + 1),
    13: (103, lambda k: k + 1),
    14: (104, lambda k: k + 1),
    15: (105, lambda k: k -15),
}

# 删除所有 "prims" 等字段的递归函数
def remove_keys(obj):
    if isinstance(obj, dict):
        return {
            k: remove_keys(v)
            for k, v in obj.items()
            if k not in ("prims")
        }
    elif isinstance(obj, list):
        return [remove_keys(item) for item in obj]
    else:
        return obj

def main():
    if len(sys.argv) < 3:
        print("Usage: script.py <input.json> <A>")
        return

    path = sys.argv[1]
    A = int(sys.argv[2])
    B = int(sys.argv[3])

    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    # 提取模板元素（id 0 和 2）
    templates = {item["id"]: item for item in data if item["id"] in [0, 1]}

    output = []

    for k in range(A, B + 1):
        if k % 16 in {1, 3, 5, 7, 9, 11, 13, 15}:
            prim_copy = 1
        else:
            prim_copy = 0

        template = copy.deepcopy(templates[prim_copy])
        template["id"] = k


        # 处理 prim_copy
        if k % 16 in {1, 3, 5, 7, 9, 11, 13, 15}:
            template["prim_copy"] = 1
        else:
            template["prim_copy"] = 0

        # 删除所有 prims、prim_copy、prim_tags 字段
        template = remove_keys(template)       
        template["prim_copy"] = prim_copy

        # 处理 worklist
        if "worklist" in template:
            for idx, witem in enumerate(template["worklist"]):
                # 修改 recv_tag
                if "recv_tag" in witem and isinstance(witem["recv_tag"], int):
                    mod = k % 16
                    witem["recv_tag"] = RECV_TAG_MAP.get(mod, witem["recv_tag"])

                # 修改 cast（数组）
                if "cast" in witem and isinstance(witem["cast"], list):
                    for i, c in enumerate(witem["cast"]):
                        if not isinstance(c, dict):
                            continue
                        mod = k % 16
                        tag, dest_fn = CAST_RULES.get(mod, (None, lambda k: k))
                        if "dest" in c:
                            if i == 0 and idx == len(template["worklist"]) - 1:
                                # 最后一项的第一个 cast 的 dest 设置为 k+16
                                c["dest"] = k + 16
                            else:
                                c["dest"] = dest_fn(k)
                        if "tag" in c:
                            c["tag"] = tag

        output.append(template)

    # 写出结果
    with open("output.json", "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2, ensure_ascii=False)

if __name__ == "__main__":
    main()
