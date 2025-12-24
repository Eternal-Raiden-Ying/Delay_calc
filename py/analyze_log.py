import sys
import numpy as np

def get_grade(value, threshold_a, threshold_b, is_relative=False):
    """
    根据赛题标准判断档次 (A/B/C)
    """
    # 如果是相对误差，输入是小数 (0.05)，为了显示方便转为百分比比较，
    # 但这里的 threshold 传入时我会直接传数值
    
    if value < threshold_a:
        return "A (Excellent)"
    elif value < threshold_b:
        return "B (Good)"
    else:
        return "C (Fail)"

def analyze_competition_score(log_file):
    # 存储列表
    # Group 1: > 50ps (关注相对误差)
    rel_errors_long = [] 
    
    # Group 2: < 50ps (关注绝对误差)
    abs_errors_short = [] 
    
    count_long = 0
    count_short = 0

    print(f"正在分析文件: {log_file} ...")

    try:
        with open(log_file, 'r', encoding='utf-8') as f:
            for line in f:
                # 跳过非数据行
                if '|' not in line or 'Golden' in line: continue
                parts = line.split('|')
                if len(parts) < 3: continue
                
                try:
                    # 读取 Golden 和 Calc
                    # 假设 log 格式: Name | Golden(ps) | Calc(ps) | ...
                    golden = float(parts[1].strip())
                    calc = float(parts[2].strip())
                    
                    if "N/A" in parts[2]: continue
                    
                    # 赛题核心分段逻辑 [cite: 89, 90]
                    if golden >= 50.0:
                        # > 50ps: 按相对误差评估
                        if golden != 0:
                            rel_err = abs(calc - golden) / golden
                            rel_errors_long.append(rel_err)
                            count_long += 1
                    else:
                        # < 50ps: 按绝对误差评估
                        abs_err = abs(calc - golden)
                        abs_errors_short.append(abs_err)
                        count_short += 1
                        
                except ValueError:
                    continue
                    
    except FileNotFoundError:
        print(f"错误: 找不到文件 {log_file}")
        return

    # =========================================================
    # 输出报告
    # =========================================================
    print("\n" + "="*70)
    print(" 🏆 EDA精英挑战赛-赛题评分报告 ")
    print("="*70)

    # ---------------------------------------------------------
    # Part 1: 大于 50ps 组 (Relative Error)
    # ---------------------------------------------------------
    print(f"\n【Group 1: Golden >= 50ps】 (样本数: {count_long})")
    if count_long > 0:
        # 计算统计量
        # 转化为百分比方便阅读，但计算 grading 时要注意单位
        data = np.array(rel_errors_long)
        mean_val = np.mean(data)
        std_val = np.std(data)
        sigma2_val = 2 * std_val
        max_val = np.max(data)

        # 评分标准 [cite: 108-116]
        # Max: <15%(A), <30%(B)
        # 2Sigma: <8%(A), <15%(B)
        # Mean: <3%(A), <5%(B)
        
        g_max = get_grade(max_val, 0.15, 0.30)
        g_2sig = get_grade(sigma2_val, 0.08, 0.15)
        g_mean = get_grade(mean_val, 0.03, 0.05)

        print(f"{'Metric':<10} | {'Value':<12} | {'Target (A-Tier)':<18} | {'Grade'}")
        print("-" * 60)
        print(f"{'Mean':<10} | {mean_val*100:6.3f}%      | < 3.0%             | {g_mean}")
        print(f"{'2-Sigma':<10} | {sigma2_val*100:6.3f}%      | < 8.0%             | {g_2sig}")
        print(f"{'Max':<10} | {max_val*100:6.3f}%      | < 15.0%            | {g_max}")
    else:
        print("无数据。")

    # ---------------------------------------------------------
    # Part 2: 小于 50ps 组 (Absolute Error)
    # ---------------------------------------------------------
    print(f"\n【Group 2: Golden < 50ps】 (样本数: {count_short})")
    if count_short > 0:
        # 计算统计量 (单位: ps)
        data = np.array(abs_errors_short)
        mean_val = np.mean(data)
        std_val = np.std(data)
        sigma2_val = 2 * std_val
        max_val = np.max(data)

        # 评分标准 [cite: 117-122]
        # Max: <20ps(A), <30ps(B)
        # 2Sigma: <20ps(A), <30ps(B)
        # Mean: <10ps(A), <15ps(B)

        g_max = get_grade(max_val, 20.0, 30.0)
        g_2sig = get_grade(sigma2_val, 20.0, 30.0)
        g_mean = get_grade(mean_val, 10.0, 15.0)

        print(f"{'Metric':<10} | {'Value':<12} | {'Target (A-Tier)':<18} | {'Grade'}")
        print("-" * 60)
        print(f"{'Mean':<10} | {mean_val:6.3f} ps     | < 10.0 ps          | {g_mean}")
        print(f"{'2-Sigma':<10} | {sigma2_val:6.3f} ps     | < 20.0 ps          | {g_2sig}")
        print(f"{'Max':<10} | {max_val:6.3f} ps     | < 20.0 ps          | {g_max}")
    else:
        print("无数据。")

    print("="*70)

if __name__ == "__main__":
    filepath=r"E:\Delay_calc\build\linear_true_34_1.log"   
    
    if len(sys.argv) > 1:
        target_file = sys.argv[1]
    analyze_competition_score(filepath)