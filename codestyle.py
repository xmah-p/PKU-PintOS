#!/usr/bin/env python3
import os, sys

def check_line_length(root, max_len=78):
    for root, _, files in os.walk(root):
        for file in files:
            if not file.endswith(('.c', '.h', '.cpp', '.hpp', '.py')): 
                continue
            path = os.path.join(root, file)
            with open(path, 'r', errors='ignore') as f:  # 忽略二进制文件
                for i, line in enumerate(f, 1):
                    if len(line.rstrip('\n')) > max_len:
                        print(f"\033[31m{path}:{i}\033[0m ({len(line)} chars)")
                        print(f"  \033[33m{line.rstrip()}\033[0m")

if __name__ == "__main__":
    check_line_length(sys.argv[1] if len(sys.argv)>1 else '.')