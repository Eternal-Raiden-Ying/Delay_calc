import pandas as pd
import matplotlib.pyplot as plt


def filter_by_column(df: pd.DataFrame, column: str, value) -> pd.DataFrame:
    """
    通用筛选函数：
    - df: 原始 DataFrame
    - column: 用于筛选的列名
    - value: 该列等于 value 的行会被保留
    """
    if column not in df.columns:
        # 列不存在时，抛出异常并在消息中附带所有列名
        available_cols = list(df.columns)
        raise KeyError(
            f"DataFrame 中不存在列 '{column}'，请检查 CSV 列名。\n"
            f"当前可用列名：{available_cols}"
        )

    df_filtered = df[df[column] == value]

    if df_filtered.empty:
        # 筛选后没有任何数据时，给出该列目前有哪些值
        unique_vals = df[column].unique()
        print(
            f"\n按列 '{column}' == {value!r} 筛选后没有任何数据。"
            f"\n该列当前一共有 {len(unique_vals)} 个不同的值（仅显示前 20 个）："
        )
        print(unique_vals[:20])
    else:
        print(f"\n按列 '{column}' == {value!r} 筛选后的数据行数: {len(df_filtered)}")

    return df_filtered


def compute_correlation_matrix(
    df: pd.DataFrame,
    csv_path: str | None = None,
) -> pd.DataFrame:
    """
    统计所有数值型列的相关系数矩阵。

    - df: 输入 DataFrame
    - csv_path: 可选，给出文件路径时会把相关系数矩阵保存为 CSV
    """
    # 只选择数值型列来做相关性分析（非数值列无法计算相关系数）
    numeric_df = df.select_dtypes(include="number")

    if numeric_df.shape[1] < 2:
        raise ValueError("数值型列少于 2 列，无法做线性相关性分析。")

    print("\n用于相关性分析的数值列：", list(numeric_df.columns))

    # 计算皮尔森相关系数矩阵（默认 method='pearson'）
    corr_matrix = numeric_df.corr()
    print("\n相关系数矩阵：")
    print(corr_matrix)

    # 如果给了 csv_path，就导出到 CSV 文件
    if csv_path is not None:
        corr_matrix.to_csv(csv_path, index=True)
        print(f"\n相关系数矩阵已保存到: {csv_path}")

    return corr_matrix


def plot_scatter(
    df: pd.DataFrame,
    x_col: str,
    y_col: str,
    cfg: dict | None = None,
) -> None:
    """
    画两个列之间的散点图。

    - df: 输入 DataFrame
    - x_col: 作为 x 轴的列名
    - y_col: 作为 y 轴的列名
    - cfg: 可选配置字典，例如：
        {
            "figsize": (6, 4),
            "alpha": 0.7,
            "marker": "o",
            "color": "tab:blue",
            "title": "my title",
            "grid": True,
            "save_path": r"...\scatter.png",
            "dpi": 120,
            "s": 1,
        }
    """
    # 检查列是否存在
    missing = [c for c in (x_col, y_col) if c not in df.columns]
    if missing:
        raise KeyError(
            f"DataFrame 中不存在列: {missing}，请检查列名。\n"
            f"当前可用列名：{list(df.columns)}"
        )

    # 默认绘图参数
    default_cfg = {
        "figsize": (6, 4),
        "alpha": 0.7,
        "marker": "o",
        "color": "tab:blue",
        "title": f"{x_col} vs {y_col}",
        "grid": True,
        "save_path": None,
        "dpi": 120,
        "s": 1,
    }
    if cfg:
        default_cfg.update(cfg)

    plt.figure(figsize=default_cfg["figsize"])
    plt.scatter(
        df[x_col],
        df[y_col],
        alpha=default_cfg["alpha"],
        marker=default_cfg["marker"],
        color=default_cfg["color"],
        s=default_cfg["s"],
    )
    plt.xlabel(x_col)
    plt.ylabel(y_col)
    plt.title(default_cfg["title"])
    if default_cfg["grid"]:
        plt.grid(True)

    # 如果指定了保存路径，则保存图片
    if default_cfg["save_path"]:
        plt.savefig(
            default_cfg["save_path"],
            dpi=default_cfg["dpi"],
            bbox_inches="tight",
        )
        print(f"散点图已保存到: {default_cfg['save_path']}")

    plt.tight_layout()
    plt.show()


def plot_xy_value_heatmap(
    df: pd.DataFrame,
    x_col: str,
    y_col: str,
    value_col: str,
    cfg: dict | None = None,
) -> None:
    """
    基于三列 (x, y, value) 画“热力图风格”的图：
    - x、y 作为坐标轴
    - value 用颜色表示大小

    适用于：
    - x, y 为离散坐标（例如网格点）
    - 或 x, y 为散点坐标，想看 value 在平面上的分布情况

    cfg 可选配置示例：
        {
            "figsize": (6, 5),
            "cmap": "viridis",
            "marker": "s",
            "s": 20,
            "alpha": 0.9,
            "vmin": None,
            "vmax": None,
            "title": "value heatmap",
            "grid": True,
            "save_path": r"...\heatmap.png",
            "dpi": 120,
        }
    """
    # 1. 检查列是否存在
    missing = [c for c in (x_col, y_col, value_col) if c not in df.columns]
    if missing:
        raise KeyError(
            f"DataFrame 中不存在列: {missing}，请检查列名。\n"
            f"当前可用列名：{list(df.columns)}"
        )

    # 2. 默认配置
    default_cfg = {
        "figsize": (6, 5),
        "cmap": "viridis",
        "marker": "s",   # 方块更像“热力图格子”
        "s": 20,
        "alpha": 0.9,
        "vmin": None,
        "vmax": None,
        "title": f"{value_col} heatmap on ({x_col}, {y_col})",
        "grid": True,
        "save_path": None,
        "dpi": 120,
    }
    if cfg:
        default_cfg.update(cfg)

    x = df[x_col]
    y = df[y_col]
    v = df[value_col]

    # 3. 画热力图风格的散点图
    plt.figure(figsize=default_cfg["figsize"])
    sc = plt.scatter(
        x,
        y,
        c=v,
        cmap=default_cfg["cmap"],
        marker=default_cfg["marker"],
        s=default_cfg["s"],
        alpha=default_cfg["alpha"],
        vmin=default_cfg["vmin"],
        vmax=default_cfg["vmax"],
    )
    plt.xlabel(x_col)
    plt.ylabel(y_col)
    plt.title(default_cfg["title"])
    if default_cfg["grid"]:
        plt.grid(True)

    # 颜色条，显示 value 的数值范围
    cbar = plt.colorbar(sc)
    cbar.set_label(value_col)

    # 4. 按需保存
    if default_cfg["save_path"]:
        plt.savefig(
            default_cfg["save_path"],
            dpi=default_cfg["dpi"],
            bbox_inches="tight",
        )
        print(f"热力图已保存到: {default_cfg['save_path']}")

    plt.tight_layout()
    plt.show()
