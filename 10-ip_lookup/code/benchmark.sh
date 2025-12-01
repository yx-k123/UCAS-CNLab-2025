#!/bin/bash

# 设置运行次数（可修改）
NUM_RUNS=50

# 初始化累加变量
basic_total=0
advance_total=0

# 循环运行程序
for i in $(seq 1 $NUM_RUNS); do
    echo "Run $i:"
    
    # 运行程序并捕获输出
    output=$(./ip_trie_tree)
    
    # 提取 basic_lookup_time（去掉 "us"）
    basic_time=$(echo "$output" | grep "basic_lookup_time" | awk -F'-' '{print $2}' | sed 's/us//')
    
    # 提取 advance_lookup_time（去掉 "us"）
    advance_time=$(echo "$output" | grep "advance_lookup_time" | awk -F'-' '{print $2}' | sed 's/us//')
    
    # 累加时间
    basic_total=$((basic_total + basic_time))
    advance_total=$((advance_total + advance_time))
    
    echo "  basic_time: ${basic_time}us, advance_time: ${advance_time}us"
done

# 计算平均值
basic_avg=$((basic_total / NUM_RUNS))
advance_avg=$((advance_total / NUM_RUNS))

# 输出结果
echo ""
echo "Average basic_lookup_time: ${basic_avg}us"
echo "Average advance_lookup_time: ${advance_avg}us"