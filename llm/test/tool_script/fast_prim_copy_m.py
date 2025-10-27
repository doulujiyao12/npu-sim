import sys
import json
import copy

def get_special_dest(A):
    """根据特殊规则生成 worklist[-1]['cast'][0]['dest'] 的值"""
    if A <= 51 and A % 8 <= 3:
        dest = A + 8
    elif 56 <= A <= 59:
        dest = A + 4
    else:
        dest = A - 8
    return dest if dest >= 0 else -1

def modify_obj(obj, new_dest_func, skip_paths, path=()):
    """递归修改'dest'字段，删除所有'prims'字段，跳过指定路径"""
    if isinstance(obj, dict):
        keys_to_delete = []
        for k, v in obj.items():
            current_path = path + (k,)
            if k == "dest" and isinstance(v, int) and current_path not in skip_paths:
                obj[k] = new_dest_func(v)
            elif k == "prims":
                keys_to_delete.append(k)
            else:
                modify_obj(v, new_dest_func, skip_paths, current_path)
        for k in keys_to_delete:
            del obj[k]
    elif isinstance(obj, list):
        for i, item in enumerate(obj):
            modify_obj(item, new_dest_func, skip_paths, path + (i,))

def main():
    if len(sys.argv) < 3:
        print("用法: python generate_elements.py input.json id1 id2 id3 ...")
        return

    input_file = sys.argv[1]
    new_ids = list(map(int, sys.argv[2:]))

    with open(input_file, 'r') as f:
        original_data = json.load(f)

    id_map = {item['id']: item for item in original_data}
    output_data = []

    for A in new_ids:
        base_id = A % 4
        if base_id not in id_map:
            print(f"错误: id = {base_id} 不存在于输入文件中")
            return

        base_element = copy.deepcopy(id_map[base_id])
        base_element["id"] = A
        base_element["prim_copy"] = base_id

        # 初始化需要跳过的路径
        skip_paths = set()

        # 特殊处理 worklist[-1]["cast"][0]["dest"]
        worklist = base_element.get("worklist", [])
        if isinstance(worklist, list) and len(worklist) > 0:
            last_idx = len(worklist) - 1
            last = worklist[-1]
            if isinstance(last, dict):
                cast = last.get("cast")
                if isinstance(cast, list) and len(cast) > 0 and isinstance(cast[0], dict):
                    special_dest = get_special_dest(A)
                    cast[0]["dest"] = special_dest
                    # 标记需要跳过的路径：worklist[-1]["cast"][0]["dest"]
                    skip_paths.add(("worklist", last_idx, "cast", 0, "dest"))

        # 定义通用 dest 替换逻辑
        def general_dest_func(_):
            if A % 4 == 0:
                return A + 1
            elif A % 4 == 1:
                return A + 2
            elif A % 4 == 2:
                return A - 2
            else:
                return A - 1

        # 修改所有 dest（除了跳过路径），并删除所有 prims
        modify_obj(base_element, general_dest_func, skip_paths)

        output_data.append(base_element)

    with open("output.json", "w") as f:
        json.dump(output_data, f, indent=2)

    print("生成完毕，结果保存在 output.json")

if __name__ == "__main__":
    main()
 