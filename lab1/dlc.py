#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
dlc - Data Lab Checker
检查 bits.c 中的函数是否符合注释中规定的操作符约束
"""

import re
import sys

# 所有合法的操作符
ALL_LEGAL_OPS = {'!', '~', '&', '^', '|', '+', '<<', '>>'}

def extract_functions(code):
    """从代码中提取函数定义及其注释"""
    # 匹配函数注释和函数体
    pattern = r'/\*\s*\n\s*\*\s*(\w+)\s*-\s*([^\n]+)\n((?:[^*]|\*(?!/))*)\*/\s*\n(\w+\s+\w+\s*\([^)]*\)\s*\{[^}]*\})'

    functions = []

    # 找到所有函数定义
    func_pattern = r'/\*\s*\n\s*\*\s*(\w+)\s*-\s*[^\n]+\n((?:[^*]|\*(?!/))*)\*/\s*\n(\w+\s+\w+\s*\([^)]*\)\s*\{)'

    for match in re.finditer(func_pattern, code):
        func_name = match.group(1)
        comment_body = match.group(2)
        func_header = match.group(3)

        # 提取合法操作符
        legal_ops_match = re.search(r'Legal ops:\s*([^\n]+)', comment_body)
        if legal_ops_match:
            legal_ops_str = legal_ops_match.group(1).strip()
            # 解析操作符列表
            legal_ops = set()
            if legal_ops_str and legal_ops_str != '':
                # 提取所有操作符
                ops_pattern = r'[!~&^|+]|<<|>>'
                legal_ops = set(re.findall(ops_pattern, legal_ops_str))

        # 提取最大操作数
        max_ops_match = re.search(r'Max ops:\s*(\d+)', comment_body)
        max_ops = int(max_ops_match.group(1)) if max_ops_match else None

        # 提取分值
        rating_match = re.search(r'Rating:\s*(\d+)', comment_body)
        rating = int(rating_match.group(1)) if rating_match else None

        # 找到函数体
        func_start = match.end()
        # 计算花括号来找到函数结束
        brace_count = 1
        func_end = func_start
        while func_end < len(code) and brace_count > 0:
            if code[func_end] == '{':
                brace_count += 1
            elif code[func_end] == '}':
                brace_count -= 1
            func_end += 1

        func_body = code[match.start():func_end]

        functions.append({
            'name': func_name,
            'legal_ops': legal_ops,
            'max_ops': max_ops,
            'rating': rating,
            'body': func_body
        })

    return functions

def count_operators(code, func_body):
    """统计函数体中使用的操作符"""
    # 移除字符串和字符常量
    code_clean = re.sub(r'"[^"]*"', '', func_body)
    code_clean = re.sub(r"'[^']*'", '', code_clean)

    # 移除注释
    code_clean = re.sub(r'//[^\n]*', '', code_clean)
    code_clean = re.sub(r'/\*[^*]*\*+(?:[^/*][^*]*\*+)*/', '', code_clean)

    # 提取函数体（去掉函数头和最外层花括号内的内容）
    body_match = re.search(r'\{(.*)\}', code_clean, re.DOTALL)
    if not body_match:
        return {}, 0

    body = body_match.group(1)

    # 统计各操作符出现次数
    ops_count = {}

    # 先统计双字符操作符
    double_ops = ['<<', '>>']
    for op in double_ops:
        count = len(re.findall(re.escape(op), body))
        if count > 0:
            ops_count[op] = count
        # 从代码中移除已统计的操作符
        body = re.sub(re.escape(op), '  ', body)

    # 统计单字符操作符
    single_ops = ['!', '~', '&', '^', '|', '+']
    for op in single_ops:
        # 使用词边界来避免误匹配（如 & 在变量名中）
        # 但对于操作符，我们需要更精确的匹配
        count = len(re.findall(r'(?<![&])' + re.escape(op) + r'(?![&=])', body))
        # 重新统计，更简单的方式
        count = body.count(op)
        if count > 0:
            ops_count[op] = count

    # 计算总操作数
    total_ops = sum(ops_count.values())

    return ops_count, total_ops

def check_function(func_info):
    """检查单个函数是否符合约束"""
    name = func_info['name']
    legal_ops = func_info['legal_ops']
    max_ops = func_info['max_ops']
    body = func_info['body']

    ops_count, total_ops = count_operators(body, body)

    errors = []

    # 检查是否使用了非法操作符
    for op, count in ops_count.items():
        if op not in legal_ops:
            errors.append(f"使用了非法操作符 '{op}' (出现 {count} 次)")

    # 检查操作数是否超限
    if max_ops is not None and total_ops > max_ops:
        errors.append(f"操作数超限: 使用了 {total_ops} 个, 最大允许 {max_ops} 个")

    return {
        'name': name,
        'legal_ops': legal_ops,
        'max_ops': max_ops,
        'ops_count': ops_count,
        'total_ops': total_ops,
        'errors': errors,
        'passed': len(errors) == 0
    }

def main():
    if len(sys.argv) < 2:
        print("用法: python3 dlc.py bits.c")
        print("检查 bits.c 文件中的函数是否符合操作符约束")
        sys.exit(1)

    filename = sys.argv[1]

    try:
        # 尝试多种编码
        encodings = ['utf-8', 'gbk', 'gb2312', 'latin-1']
        code = None
        for encoding in encodings:
            try:
                with open(filename, 'r', encoding=encoding) as f:
                    code = f.read()
                break
            except UnicodeDecodeError:
                continue

        if code is None:
            # 如果所有编码都失败，使用二进制模式读取并忽略错误
            with open(filename, 'rb') as f:
                code = f.read().decode('utf-8', errors='ignore')
    except FileNotFoundError:
        print(f"错误: 找不到文件 '{filename}'")
        sys.exit(1)
    except Exception as e:
        print(f"错误: 读取文件失败 - {e}")
        sys.exit(1)

    print(f"正在检查 {filename}...\n")
    print("=" * 60)

    functions = extract_functions(code)

    if not functions:
        print("未找到任何函数定义")
        sys.exit(0)

    passed_count = 0
    failed_count = 0

    for func_info in functions:
        result = check_function(func_info)

        if result['passed']:
            passed_count += 1
            status = "✓ 通过"
        else:
            failed_count += 1
            status = "✗ 失败"

        print(f"\n函数: {result['name']}")
        print(f"  合法操作符: {', '.join(sorted(result['legal_ops'])) if result['legal_ops'] else '无'}")
        print(f"  最大操作数: {result['max_ops']}")
        print(f"  实际操作数: {result['total_ops']}")
        print(f"  操作符统计: {result['ops_count']}")
        print(f"  状态: {status}")

        if result['errors']:
            print(f"  错误:")
            for error in result['errors']:
                print(f"    - {error}")

    print("\n" + "=" * 60)
    print(f"检查完成: {passed_count} 个通过, {failed_count} 个失败")

    if failed_count > 0:
        sys.exit(1)

if __name__ == '__main__':
    main()
