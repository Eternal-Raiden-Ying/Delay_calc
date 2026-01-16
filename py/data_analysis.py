import pandas as pd
import matplotlib.pyplot as plt
from utils import (
    filter_by_column,
    compute_correlation_matrix,
    plot_scatter,
    plot_xy_value_heatmap
)

# 设置全局字体为 Times New Roman
plt.rcParams['font.family'] = 'Times New Roman'


def load_csv(csv_path: str) -> pd.DataFrame:
    """
    从绝对路径读取 CSV 文件，返回 DataFrame。
    """
    # header=0 表示第一行是列名；encoding 按实际文件情况调整
    df = pd.read_csv(csv_path, header=0)
    print("CSV 列名：", list(df.columns))
    return df


def main():
    # TODO: 把下面的路径替换为你自己的 CSV 绝对路径
    csv_path = r"D:\Documents\Coding\Projects\Delay_calc\Delay_calc\analyze_data_group_1.csv"

    # 1. 读取 CSV
    df = load_csv(csv_path)

    # 2. 使用通用筛选函数：按 leaf_node == 2 进行筛选
    df = filter_by_column(df, column="input_pin_num", value=2)

    # 3. 计算相关系数矩阵（如果需要导出 CSV，在这里填写路径）
    corr_csv_path = None
    # 例如：
    # corr_csv_path = r"D:\Documents\Coding\Projects\Delay_calc\Delay_calc\csv\analysis_leaf3_result.csv"
    # compute_correlation_matrix(df, csv_path=corr_csv_path)

    plt.figure()
    plt.scatter(df['LengthRatio'], -df['RelError'], s=2, c='red')  # s 越小点越小
    plt.xlabel("length_ratio")
    plt.ylabel("rel_err")
    plt.title("rel_err vs length_ratio (Group 1)", fontsize=18)
    plt.grid(True)
    plt.ylim(-0.5,1.25)
    plt.tight_layout()
    plt.show()

    # df_small_golden = df[df["Golden(ps)"] < 50]
    # df_large_golden = df[df["Golden(ps)"] > 50]

    # # 一行两列子图
    # fig, axes = plt.subplots(1, 2, figsize=(10, 4))

    # # 左图：Golden(ps) < 50: 画 AbsError vs LengthRatio
    # axes[0].scatter(df_small_golden["LengthRatio"], df_small_golden["AbsError"], s=1)
    # axes[0].set_xlabel("LengthRatio")
    # axes[0].set_ylabel("AbsError")
    # axes[0].set_title("AbsError vs LengthRatio (Golden(ps) < 50)")
    # axes[0].grid(True)

    # # 右图：Golden(ps) > 50: 画 RelError vs LengthRatio
    # axes[1].scatter(df_large_golden["LengthRatio"], df_large_golden["RelError"], s=1)
    # axes[1].set_xlabel("LengthRatio")
    # axes[1].set_ylabel("RelError")
    # axes[1].set_title("RelError vs LengthRatio (Golden(ps) > 50)")
    # axes[1].grid(True)

    # plt.tight_layout()
    # plt.show()


if __name__ == "__main__":
    main()
