import pandas as pd
import xgboost as xgb
import m2cgen as m2c
import numpy as np
import os
import sys
from sklearn.metrics import mean_absolute_percentage_error

# ==============================================================================
# ★ FIX 1: 增加递归深度，防止导出报错
# ==============================================================================
sys.setrecursionlimit(200000)

# ==============================================================================
# 1. 全局配置开关 (USER CONFIGURATION)
# ==============================================================================
class Config:
    # --- 开关 1: 特征数量 (4 或 34) ---
    # 4  : num_stages, rho, Elmore_ps, subtree_Elmore_ps
    # 34 : 上述4个 + R1-10 + C1-10 + Stage1-10
    FEATURE_MODE = 34
    
    # --- 开关 2: 拟合方式 ('ratio', 'delta', 'linear') ---
    # ratio : y = Golden/Elmore (推荐)
    # delta : y = Golden-Elmore
    # linear: y = Golden
    FIT_MODE = 'linear' 
    
    # --- 开关 3: 组别 (0 或 1) ---
    GROUP_ID = 1  # 对应你提供的 Group1
    
    # --- 开关 4: 分桶模式 (True: 分开训练, False: 统一训练) ---
    # True : 按 50ps 切分为 Short/Long 两个模型
    SPLIT_MODE = True 

    # --- 路径配置 (模板化) ---
    BASE_DIR = r"E:\Delay_calc\Data"
    # 根据 Group ID 自动匹配文件名
    # CSV: path_features_group1.csv
    CSV_PATH_TEMPLATE = os.path.join(BASE_DIR, "path_features_group{}.csv")
    # TXT: Data/delay_data/Group1.txt
    TXT_PATH_TEMPLATE = os.path.join(BASE_DIR, "delay_data", "Group{}.txt")

# ==============================================================================
# 2. 数据处理模块 (复刻你的逻辑)
# ==============================================================================
def load_and_merge_data(group_id):
    csv_path = Config.CSV_PATH_TEMPLATE.format(group_id)
    txt_path = Config.TXT_PATH_TEMPLATE.format(group_id)
    
    print(f"[INFO] Loading Group {group_id}...")
    print(f"       CSV: {csv_path}")
    print(f"       TXT: {txt_path}")

    # 1. 读取 CSV
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"CSV not found: {csv_path}")
    df_feat = pd.read_csv(csv_path)
    df_feat.columns = [c.strip() for c in df_feat.columns] # 去空格

    # 2. 读取 Golden TXT
    if not os.path.exists(txt_path):
        raise FileNotFoundError(f"TXT not found: {txt_path}")
    
    golden_data = []
    with open(txt_path, 'r', encoding='utf-8') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 3:
                try:
                    # 逻辑与你的一致: [ -> _
                    p_name = parts[1].replace('[', '_').replace(']', '_')
                    d_ns = float(parts[-1])
                    golden_data.append({'PinName': p_name, 'Golden': d_ns * 1000.0})
                except: continue
    df_gold = pd.DataFrame(golden_data)

    # 3. 合并 (Inner Join)
    # 你的 CSV 应该包含 'real_input_name'
    if 'real_input_name' not in df_feat.columns:
        raise ValueError("CSV missing 'real_input_name' column!")

    print(f"[INFO] Merging data...")
    df = pd.merge(df_feat, df_gold, left_on='real_input_name', right_on='PinName', how='inner')
    
    # 4. 基础清洗 (保留 Elmore_ps > 0)
    # ★ FIX 2: 严格使用 'Elmore_ps'，不进行重命名，防止混淆
    if 'Elmore_ps' not in df.columns:
        raise ValueError("CSV missing 'Elmore_ps' column!")
        
    df = df[(df['Elmore_ps'] > 1e-6) & (df['Golden'] > 1e-6)].copy()
    
    return df

def get_feature_columns(mode):
    # ★ FIX 3: 特征名称严格对齐
    scalar_feats = ['num_stages', 'rho', 'Elmore_ps', 'subtree_Elmore_ps']
    
    if mode == 4:
        return scalar_feats
    elif mode == 34:
        vec_r     = [f'R{k}' for k in range(1, 11)]
        vec_c     = [f'C_ds{k}' for k in range(1, 11)]
        vec_stage = [f'StageDelay{k}' for k in range(1, 11)]
        return scalar_feats + vec_r + vec_c + vec_stage
    else:
        raise ValueError("FEATURE_MODE must be 4 or 34")

def prepare_target(df, fit_mode):
    # 根据拟合开关生成 Target
    if fit_mode == 'ratio':
        df['Target'] = df['Golden'] / df['Elmore_ps']
        df = df[(df['Target'] < 10.0) & (df['Target'] > 0.1)] # 清洗极端值
        base_score = 1.0 # m2cgen 默认基准
        
    elif fit_mode == 'delta':
        df['Target'] = df['Golden'] - df['Elmore_ps']
        base_score = 0.0
        
    elif fit_mode == 'linear':
        df['Target'] = df['Golden']
        base_score = 0.0
        
    else:
        raise ValueError(f"Unknown FIT_MODE: {fit_mode}")
        
    return df, base_score

# ==============================================================================
# 3. 训练函数 (严格复刻你的参数)
# ==============================================================================
def train_xgboost(X, y, base_score):
    # 参数与你提供的完全一致
    model = xgb.XGBRegressor(
        device='cuda', 
        tree_method='hist',
        n_estimators=500,
        max_depth=6, 
        learning_rate=0.05, 
        subsample=0.8,
        colsample_bytree=0.8, 
        base_score=base_score,
        objective='reg:squarederror'
    )
    # ★ FIX 4: 使用全量数据训练，不再拆分验证集，保证最终模型效果最大化
    model.fit(X, y)
    return model

def evaluate_on_train(model, X, y_true, elmore, fit_mode, prefix="Set"):
    # 在训练集上验证 (这就和你原本的代码逻辑一致了)
    preds = model.predict(X)
    
    if fit_mode == 'ratio':
        final_delay = elmore * preds
    elif fit_mode == 'delta':
        final_delay = elmore + preds
    elif fit_mode == 'linear':
        final_delay = preds
        
    mape = mean_absolute_percentage_error(y_true, final_delay)
    print(f"  -> {prefix} MAPE: {mape*100:.4f}%")

# ==============================================================================
# 4. 主程序
# ==============================================================================
def main():
    # 生成文件名: ratio_true_34_1.h
    out_file = f"{Config.FIT_MODE}_{str(Config.SPLIT_MODE).lower()}_{Config.FEATURE_MODE}_{Config.GROUP_ID}.h"
    
    print("="*60)
    print(f"★ SYSTEMATIC TRAIN (FIXED)")
    print(f"   Group ID    : {Config.GROUP_ID}")
    print(f"   Features    : {Config.FEATURE_MODE}")
    print(f"   Fit Mode    : {Config.FIT_MODE}")
    print(f"   Split Mode  : {Config.SPLIT_MODE}")
    print(f"   Target File : {out_file}")
    print("="*60)

    # 1. 加载数据
    try:
        df = load_and_merge_data(Config.GROUP_ID)
    except Exception as e:
        print(f"[Error] {e}")
        return

    # 2. 准备特征
    feats = get_feature_columns(Config.FEATURE_MODE)
    # 检查列是否存在
    missing = [c for c in feats if c not in df.columns]
    if missing:
        print(f"[Error] Missing features: {missing}")
        return

    # 3. 准备 Target
    df, base_score = prepare_target(df, Config.FIT_MODE)
    print(f"[INFO] Valid Samples: {len(df)}")
    
    c_code_output = ""
    
    # 4. 分支逻辑
    if Config.SPLIT_MODE:
        # === 分桶模式 (复刻 train_hard_split) ===
        split_val = 50.0
        mask_short = df['Elmore_ps'] < split_val
        
        df_short = df[mask_short].copy()
        df_long = df[~mask_short].copy()
        
        print("-" * 40)
        print(f"Split Threshold: {split_val} ps")
        print(f"Short Samples: {len(df_short)}")
        print(f"Long  Samples: {len(df_long)}")
        print("-" * 40)

        # Train Short
        if len(df_short) > 0:
            print("\n[Training] Short Model...")
            model_s = train_xgboost(df_short[feats], df_short['Target'], base_score)
            evaluate_on_train(model_s, df_short[feats], df_short['Golden'], df_short['Elmore_ps'], Config.FIT_MODE, "Short")
            c_code_output += m2c.export_to_c(model_s, function_name="score_short") + "\n\n"
        
        # Train Long
        if len(df_long) > 0:
            print("\n[Training] Long Model...")
            model_l = train_xgboost(df_long[feats], df_long['Target'], base_score)
            evaluate_on_train(model_l, df_long[feats], df_long['Golden'], df_long['Elmore_ps'], Config.FIT_MODE, "Long")
            c_code_output += m2c.export_to_c(model_l, function_name="score_long") + "\n"
            
    else:
        # === 统一模式 ===
        print("\n[Training] Unified Model...")
        model = train_xgboost(df[feats], df['Target'], base_score)
        evaluate_on_train(model, df[feats], df['Golden'], df['Elmore_ps'], Config.FIT_MODE, "Unified")
        c_code_output += m2c.export_to_c(model, function_name="score") + "\n"

    # 5. 导出
    with open(out_file, "w") as f:
        f.write(f"// Generated by train_systematic_fixed.py\n")
        f.write(f"// Config: Fit={Config.FIT_MODE}, Split={Config.SPLIT_MODE}, Feat={Config.FEATURE_MODE}, Group={Config.GROUP_ID}\n")
        f.write(f"// Feature Order ({len(feats)}): {', '.join(feats)}\n\n")
        f.write(c_code_output)
        
    print(f"\n[Done] Header file saved to: {out_file}")

if __name__ == "__main__":
    main()