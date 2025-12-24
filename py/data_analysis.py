import pandas as pd
import matplotlib.pyplot as plt
from utils import (
    filter_by_column,
    compute_correlation_matrix,
    plot_scatter,
    plot_xy_value_heatmap
)


def load_csv(csv_path: str) -> pd.DataFrame:
    """
    从绝对路径读取 CSV 文件，返回 DataFrame。
    """
    # header=0 表示第一行是列名；encoding 按实际文件情况调整
    df = pd.read_csv(csv_path, header=0)
    print("CSV 列名：", list(df.columns))
    print("数据前 5 行：")
    print(df.head())
    return df


def main():
    # TODO: 把下面的路径替换为你自己的 CSV 绝对路径
    csv_path = r"D:\Documents\Coding\Projects\Delay_calc\Delay_calc\csv\error_summary_group0_update.csv"

    # 1. 读取 CSV
    df = load_csv(csv_path)

    # 2. 使用通用筛选函数：按 leaf_node == 2 进行筛选
    df = filter_by_column(df, column="input_count_net", value=2)

    # 3. 计算相关系数矩阵（如果需要导出 CSV，在这里填写路径）
    corr_csv_path = None
    # 例如：
    # corr_csv_path = r"D:\Documents\Coding\Projects\Delay_calc\Delay_calc\csv\analysis_leaf3_result.csv"
    compute_correlation_matrix(df, csv_path=corr_csv_path)

    # 4. 计算比值 branch_R_ohm / max_R_dist_inputs 并与 rel_err 画散点图
    x_ratio = df["branch_R_ohm"] / df["max_R_dist_inputs"]
    y_rel_err = df["rel_err"]

    plt.figure()
    plt.scatter(x_ratio, y_rel_err, s=1)  # s 越小点越小
    plt.xlabel("branch_R_ohm / max_R_dist_inputs")
    plt.ylabel("rel_err")
    plt.title("rel_err vs branch_R_ohm / max_R_dist_inputs")
    plt.grid(True)
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()
