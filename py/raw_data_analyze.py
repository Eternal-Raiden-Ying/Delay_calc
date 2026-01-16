import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


# 设置全局字体为 Times New Roman
plt.rcParams['font.family'] = 'Times New Roman'


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
    csv_path = r"e:\Documents_E\xwechat_files\wxid_zmmtkbdw590q22_3009\msg\file\2025-12\error_summary_group0_update(1).csv"

    # 1. 读取 CSV
    df = load_csv(csv_path)

    df_smaller = filter_by_column(df, column="metric_type", value='ABS')

    abs_error = np.abs(np.array(df_smaller['err_ps']))
    print("**************TEST***************")
    print("abs_error_mean:", np.mean(abs_error))
    print("abs_error_max:", np.max(abs_error))
    print("abs_error_2sigma:", 2*np.var(abs_error)**0.5)

    df_larger = filter_by_column(df, column="metric_type", value='REL')
    golden = np.array(df_larger['std_ps'])
    calc=np.array(df_larger['calc_ps'])
    # rel_error = np.abs(np.array(df_larger['rel_err']))
    rel_error = 100 * np.abs(golden - calc) / golden
    print("rel_error_mean:", np.mean(rel_error))
    print("rel_error_max:", np.max(rel_error))
    print("rel_error_2sigma:", 2*np.var(rel_error)**0.5)

    raw_res = {}
    for i in range(len(df)):
        key = str(df.iloc[i]['net_name']) + str(df.iloc[i]['real_input_name'])
        raw_res[key] = df.iloc[i]['calc_ps']
    
    revised_res_path = r"D:\Documents\Coding\Projects\Delay_calc\Delay_calc\features\delay0.txt"
    revised_res = {}
    with open(revised_res_path, 'r') as file:  # golden results, please put in the same dir. as your own results (delay0.txt)
        for line in file:
            lines = line.strip("\n").split(" ")
            revised_res[lines[0]+lines[1]] = lines[2]
    
    diff_name = []
    diff_res = []
    print(len(raw_res.keys()))
    # for key in revised_res:
    #     if key not in raw_res:
    #         print("Missing key in raw_res:", key)
    for key in raw_res:
        # if key not in revised_res:
        #     print("Missing key in revised_res:", key)
        if key in revised_res:
            golden = float(raw_res[key])
            calc = float(revised_res[key])
            # if abs(calc - golden) > 1e-3:
            diff_res.append(calc - golden)
            diff_name.append(key)
    
    print(np.nonzero(diff_res))
    count = 0
    for i in np.nonzero(diff_res)[:1][0]:
        if count > 10:
            break
        print(diff_name[i], diff_res[i])
        count += 1


if __name__ == "__main__":
    main()
