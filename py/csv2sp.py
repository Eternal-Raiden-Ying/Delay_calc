from __future__ import annotations

import csv
import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Tuple, List


@dataclass
class RcNode:
    label: str
    ground_cap: float      # 单位: fF
    is_output: bool = False
    is_input: bool = False
    degree: int = 0
    pin_cap: float = 0.0   # 单位: pF


def _load_rc_nodes(path: str | Path) -> Dict[int, RcNode]:
    """
    读取 rc_nodes.csv，返回:
        { node_id: RcNode(...) }
    - 若存在 degree 列且为 0，则跳过该节点
    """
    path = Path(path)
    nodes: Dict[int, RcNode] = {}
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            node_id = int(row["id"])

            # 检测 degree == 0 的节点并删除（不导出到 SPICE）
            deg_raw = row.get("degree")
            try:
                deg = int(deg_raw) if deg_raw is not None and deg_raw != "" else 0
            except ValueError:
                deg = 0
            if deg_raw is not None and deg == 0:
                # 有 degree 列且为 0，认为是孤立节点，跳过
                continue

            label = (row.get("label") or "").strip().strip('"')
            ground_cap = float(row.get("ground_cap", 0.0))

            is_output = bool(int(row.get("is_output", 0)))
            is_input = bool(int(row.get("is_input", 0)))
            pin_cap = float(row.get("pin_cap", 0.0))

            nodes[node_id] = RcNode(
                label=label,
                ground_cap=ground_cap,
                is_output=is_output,
                is_input=is_input,
                degree=deg,
                pin_cap=pin_cap,
            )
    return nodes


def _load_rc_edges(path: str | Path) -> List[Tuple[int, int, float]]:
    """
    读取 rc_edges.csv，返回:
        [ (source_id, target_id, resistance_ohm), ... ]
    """
    path = Path(path)
    edges: List[Tuple[int, int, float]] = []
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            src = int(row["source"])
            tgt = int(row["target"])
            res = float(row["res_ohm"])
            edges.append((src, tgt, res))
    return edges


def rc_csv_to_spice(
    rc_nodes_csv: str | Path,
    rc_edges_csv: str | Path,
    title: str = "RC network from CSV",
    node_prefix: str = "n",
    ground_node: str = "0",
    supply_node: str = "Supply",
    supply_voltage: float = 1.0,
    driver_res_ohm: float = 0.0,
) -> str:
    """
    由 rc_nodes / rc_edges CSV 构建 SPICE RC netlist 字符串。

    - 所有电容均接地: Cxx <node> 0 <cap>
      * ground_cap 为 fF，写成 <value>f
      * input pin 的 pin_cap 为 pF，写成 <value>p
    - 所有电阻来自 edges: Rxx <node1> <node2> <res_ohm> （单位: 欧姆）
    - 输出 pin（is_output==1）接电源 Supply:
      VDD Supply 0 <supply_voltage>
      RDRVx Supply n<id> <driver_res_ohm>
    - degree == 0 的节点已在读取阶段过滤，不会出现在 netlist 中
    """
    nodes = _load_rc_nodes(rc_nodes_csv)
    edges = _load_rc_edges(rc_edges_csv)

    def node_name(node_id: int) -> str:
        # 避免与 SPICE 全局地节点 "0" 冲突
        return f"{node_prefix}{node_id}"

    lines: List[str] = [f"* {title}", ""]

    # 输出 pin -> Supply
    output_node_ids = [nid for nid, n in nodes.items() if n.is_output]
    if output_node_ids:
        # 电源源（简单 DC 源，可按需在调用时调整电压）
        lines.append(f"VDD {supply_node} {ground_node} {supply_voltage}")
        # 用 0Ω（或给定）电阻将 Supply 连接到每个输出节点
        for idx, nid in enumerate(output_node_ids, start=1):
            n = node_name(nid)
            lines.append(f"RDRV{idx} {supply_node} {n} {driver_res_ohm}")
        lines.append("")

    # 电阻（欧姆）
    for idx, (src, tgt, res) in enumerate(edges, start=1):
        n1 = node_name(src)
        n2 = node_name(tgt)
        lines.append(f"R{idx} {n1} {n2} {res}")

    # 电容：ground_cap(fF) + input pin 的 pin_cap(pF)
    for node_id, node in sorted(nodes.items()):
        n = node_name(node_id)

        # 节点本身对地电容：fF -> 使用 SPICE 缩写 'f'
        if node.ground_cap > 0.0:
            lines.append(f"C{node_id} {n} {ground_node} {node.ground_cap}f")

        # input pin 负载电容：pF -> 使用 SPICE 缩写 'p'
        if node.is_input and node.pin_cap > 0.0:
            lines.append(f"Cpin{node_id} {n} {ground_node} {node.pin_cap}p")

    lines.append("")
    lines.append(".end")
    return "\n".join(lines)


def write_rc_spice(
    rc_nodes_csv: str | Path,
    rc_edges_csv: str | Path,
    out_spice_path: str | Path,
    **kwargs,
) -> Path:
    """
    从 rc_nodes / rc_edges 生成 RC SPICE netlist 文件。

    示例:
        write_rc_spice("rc_nodes.csv", "rc_edges.csv", "rc.sp")

    其他参数透传给 rc_csv_to_spice (title, node_prefix, ground_node 等)。
    """
    netlist = rc_csv_to_spice(rc_nodes_csv, rc_edges_csv, **kwargs)
    out_path = Path(out_spice_path)
    out_path.write_text(netlist)
    return out_path


def main(argv: List[str] | None = None) -> None:
    """
    命令行入口：从 rc_nodes.csv / rc_edges.csv 生成 SPICE netlist。
    """
    parser = argparse.ArgumentParser(description="Convert RC CSV to SPICE netlist.")
    proj_root = Path(__file__).resolve().parents[1]

    parser.add_argument(
        "--nodes",
        type=Path,
        default=proj_root / "rc_nodes.csv",
        help="rc_nodes.csv 路径（默认: 工程根目录下 rc_nodes.csv）",
    )
    parser.add_argument(
        "--edges",
        type=Path,
        default=proj_root / "rc_edges.csv",
        help="rc_edges.csv 路径（默认: 工程根目录下 rc_edges.csv）",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=proj_root / "rc_network.sp",
        help="输出 SPICE netlist 路径（默认: 工程根目录下 rc_network.sp）",
    )
    parser.add_argument(
        "--title",
        type=str,
        default="RC network from CSV",
        help="SPICE netlist 标题",
    )

    args = parser.parse_args(argv)

    out_path = write_rc_spice(
        args.nodes,
        args.edges,
        args.out,
        title=args.title,
    )
    print(f"SPICE netlist written to: {out_path}")


if __name__ == "__main__":
    main()
