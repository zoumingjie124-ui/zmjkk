"""开环阶跃响应辨识 + PID 初值计算（整定辅助，纯标准库）。

流程（详见 docs/TUNING.md）：
    1. 固件在 STANDBY 下，用 monitor_cli.py --csv 记录；
       稳定在环境温度后，开环给一个固定占空比（例如 30%），直到温度基本不再变化；
    2. 本脚本读取 CSV（time_s,temp_c,...,duty_pct,...），用 Smith 两点法拟合
       一阶滞后 + 纯滞后（FOPDT）模型：   G(s) = K · e^(-θs) / (τs + 1)
       K：单位占空比对应的稳态温升（℃/%）；τ：时间常数（s）；θ：纯滞后（s）
    3. 由模型给出 PI（IMC/λ 整定）和 PID（Cohen-Coon）初值，并按固件允许范围检查。

用法：
    python host/tune_helper.py run1.csv
    python host/tune_helper.py run1.csv --step-time 12.0 --lambda-factor 1.5
    python host/tune_helper.py --selftest

注意：这里算出来的只是**初值**，必须再做闭环阶跃实测并按超调/调节时间微调，
不要把计算结果当最终参数写进简历。
"""

from __future__ import annotations

import argparse
import csv
import math
import sys

# 与 App/Inc/control.h 中的参数合法范围保持一致
KP_MAX, KI_MAX, KD_MAX = 50.0, 5.0, 20.0


class TuneError(Exception):
    pass


def load_csv(path: str):
    """读取 CSV，返回 (t, y, u) 三个 float 列表。支持列名 time_s/t, temp_c/temp, duty_pct/duty。"""
    with open(path, newline="", encoding="utf-8") as f:
        rd = csv.DictReader(f)
        cols = {c.strip().lower(): c for c in (rd.fieldnames or [])}

        def pick(*names):
            for n in names:
                if n in cols:
                    return cols[n]
            raise TuneError(f"CSV 缺少列 {names}，现有列：{list(cols)}")

        ct, cy, cu = pick("time_s", "t", "time"), pick("temp_c", "temp", "y"), pick("duty_pct", "duty", "u")
        t, y, u = [], [], []
        for row in rd:
            try:
                t.append(float(row[ct]))
                y.append(float(row[cy]))
                u.append(float(row[cu]))
            except (ValueError, TypeError):
                continue                                  # 跳过坏行（如 ERR 读数）
    if len(t) < 20:
        raise TuneError("有效数据点少于 20 个，无法辨识")
    return t, y, u


def _smooth(x, n=5):
    """滑动平均（边缘用较短窗口），抑制 ADC 量化噪声。"""
    out = []
    h = n // 2
    for i in range(len(x)):
        a, b = max(0, i - h), min(len(x), i + h + 1)
        out.append(sum(x[a:b]) / (b - a))
    return out


def _cross_time(t, y, level, rising):
    """第一次穿越 level 的时刻（线性插值）；找不到返回 None。"""
    for i in range(1, len(t)):
        a, b = y[i - 1], y[i]
        if (rising and a < level <= b) or ((not rising) and a > level >= b):
            if b == a:
                return t[i]
            return t[i - 1] + (level - a) / (b - a) * (t[i] - t[i - 1])
    return None


def identify(t, y, u, step_time=None):
    """Smith 两点法拟合 FOPDT。返回 dict（K, tau, theta, y0, y_inf, du, dy, warnings）。"""
    warnings = []
    n = len(t)
    u0 = sorted(u[:5])[len(u[:5]) // 2]

    if step_time is None:
        i0 = next((i for i in range(n) if abs(u[i] - u0) >= 1.0), None)
        if i0 is None:
            raise TuneError("没有检测到占空比阶跃（占空比一直没变过）；可用 --step-time 指定")
        step_time = t[i0]
    else:
        i0 = next((i for i in range(n) if t[i] >= step_time), None)
        if i0 is None:
            raise TuneError("--step-time 超出数据范围")

    u1 = sorted(u[i0:i0 + 5])[len(u[i0:i0 + 5]) // 2]
    du = u1 - u0
    if abs(du) < 1.0:
        raise TuneError(f"阶跃幅度太小（{du:.2f}%），无法辨识")

    ys = _smooth(y, 5)
    pre = ys[max(0, i0 - 5):i0 + 1]
    y0 = sum(pre) / len(pre)
    tail = ys[int(n * 0.9):]
    y_inf = sum(tail) / len(tail)
    dy = y_inf - y0
    if abs(dy) < 0.5:
        raise TuneError(f"温升只有 {dy:.2f}℃，太小（噪声会淹没）；加大阶跃幅度或延长记录")

    # 末段是否已稳定：末 10% 内的变化量占总温升的比例
    drift = abs(tail[-1] - tail[0]) / abs(dy)
    if drift > 0.02:
        warnings.append(f"末段仍在变化（占总温升 {drift*100:.1f}%），可能未到稳态，K 会偏小，请延长记录")

    rising = dy > 0
    lv28, lv63 = y0 + 0.283 * dy, y0 + 0.632 * dy
    t28 = _cross_time(t, ys, lv28, rising)
    t63 = _cross_time(t, ys, lv63, rising)
    if t28 is None or t63 is None:
        raise TuneError("曲线没有穿过 28.3%/63.2% 水平，记录不完整")
    t28 -= step_time
    t63 -= step_time

    tau = 1.5 * (t63 - t28)
    theta = t63 - tau
    dts = sorted(t[i] - t[i - 1] for i in range(1, n))
    dt_med = dts[len(dts) // 2]
    if theta < dt_med:
        warnings.append(f"θ={theta:.2f}s 小于采样间隔 {dt_med:.2f}s，按 {dt_med:.2f}s 处理")
        theta = dt_med
    if tau <= 0:
        raise TuneError("τ<=0，曲线形状不符合一阶模型（可能有振荡或干扰）")

    return {"K": dy / du, "tau": tau, "theta": theta, "y0": y0, "y_inf": y_inf,
            "du": du, "dy": dy, "step_time": step_time, "dt": dt_med, "warnings": warnings}


def imc_pi(K, tau, theta, lam_factor=1.0):
    """IMC/λ 整定 PI。λ = lam_factor · max(θ, 0.3τ)：因子越大越保守（超调小、响应慢）。
    固件是位置式并联形式 u = Kp·e + Ki·∫e·dt，所以 Ki = Kp / Ti，Ti = τ。"""
    lam = lam_factor * max(theta, 0.3 * tau)
    kp = tau / (K * (lam + theta))
    return {"kp": kp, "ki": kp / tau, "kd": 0.0, "lambda": lam}


def cohen_coon_pid(K, tau, theta):
    """Cohen-Coon PID（θ/τ 越大越适用；响应较激进，必须再实测微调）。"""
    r = theta / tau
    kp = (1.0 / K) * (tau / theta) * (4.0 / 3.0 + r / 4.0)
    ti = theta * (32.0 + 6.0 * r) / (13.0 + 8.0 * r)
    td = theta * 4.0 / (11.0 + 2.0 * r)
    return {"kp": kp, "ki": kp / ti, "kd": kp * td}


def range_warnings(name, p):
    out = []
    if p["kp"] > KP_MAX:
        out.append(f"{name}: Kp={p['kp']:.2f} 超过固件上限 {KP_MAX}")
    if p["ki"] > KI_MAX:
        out.append(f"{name}: Ki={p['ki']:.3f} 超过固件上限 {KI_MAX}")
    if p["kd"] > KD_MAX:
        out.append(f"{name}: Kd={p['kd']:.2f} 超过固件上限 {KD_MAX}")
    return out


def report(m, lam_factor=1.0):
    pi = imc_pi(m["K"], m["tau"], m["theta"], lam_factor)
    cc = cohen_coon_pid(m["K"], m["tau"], m["theta"])
    ratio = m["theta"] / m["tau"]
    lines = [
        "=== FOPDT 辨识结果 ===",
        f"阶跃：{m['du']:+.1f}% @ t={m['step_time']:.1f}s；温度 {m['y0']:.2f}℃ -> {m['y_inf']:.2f}℃（Δ={m['dy']:+.2f}℃）",
        f"K = {m['K']:.4f} ℃/%    τ = {m['tau']:.1f} s    θ = {m['theta']:.1f} s    θ/τ = {ratio:.2f}",
        "",
        f"[PI · IMC/λ]  λ={pi['lambda']:.1f}s  Kp={pi['kp']:.3f}  Ki={pi['ki']:.4f}  Kd=0      <- 首选起点（稳健）",
        f"[PID · Cohen-Coon]  Kp={cc['kp']:.3f}  Ki={cc['ki']:.4f}  Kd={cc['kd']:.3f}       <- 响应更快但超调更大，谨慎",
    ]
    warns = list(m["warnings"]) + range_warnings("PI", pi) + range_warnings("PID", cc)
    if ratio > 0.5:
        warns.append(f"θ/τ={ratio:.2f} 较大（滞后重）：PID 提升有限，建议降低 Kp/增大 λ 因子，并关注振荡")
    if ratio < 0.05:
        warns.append(f"θ/τ={ratio:.2f} 很小：Cohen-Coon 增益会很大，请改用 PI，并留意噪声")
    if warns:
        lines.append("")
        lines.append("注意：")
        lines += [f"  - {w}" for w in warns]
    lines.append("")
    lines.append("下一步：用上位机下发参数 -> 闭环阶跃实测 -> 记录超调/调节时间/稳态误差 -> 按结果微调（见 docs/TUNING.md）")
    return "\n".join(lines)


# ---------------------------------------------------------------- 自测
def synth_fopdt(K=0.5, tau=60.0, theta=8.0, du=30.0, y0=25.0, dt=0.5, t_total=600.0,
                t_step=20.0, noise=0.0, seed=1):
    import random
    rnd = random.Random(seed)
    t, y, u = [], [], []
    k = 0
    while k * dt <= t_total:
        tt = k * dt
        uu = du if tt >= t_step else 0.0
        if tt < t_step + theta:
            yy = y0
        else:
            yy = y0 + K * du * (1.0 - math.exp(-(tt - t_step - theta) / tau))
        yy += rnd.uniform(-noise, noise)
        t.append(tt); y.append(yy); u.append(uu)
        k += 1
    return t, y, u


def selftest() -> int:
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(f"  [{'PASS' if cond else 'FAIL'}] {msg}")
        ok = ok and cond

    print("tune_helper selftest:")
    t, y, u = synth_fopdt()
    m = identify(t, y, u)
    check(abs(m["K"] - 0.5) < 0.01, f"noise-free K={m['K']:.4f} (true 0.5)")
    check(abs(m["tau"] - 60.0) < 3.0, f"noise-free tau={m['tau']:.1f} (true 60)")
    check(abs(m["theta"] - 8.0) < 1.5, f"noise-free theta={m['theta']:.1f} (true 8)")
    t, y, u = synth_fopdt(noise=0.05)
    m = identify(t, y, u)
    check(abs(m["K"] - 0.5) < 0.02, f"noisy K={m['K']:.4f}")
    check(abs(m["tau"] - 60.0) < 8.0, f"noisy tau={m['tau']:.1f}")
    check(abs(m["theta"] - 8.0) < 3.0, f"noisy theta={m['theta']:.1f}")
    pi = imc_pi(0.5, 60.0, 8.0)
    check(pi["kp"] > 0 and abs(pi["ki"] - pi["kp"] / 60.0) < 1e-9, f"PI Kp={pi['kp']:.3f} Ki=Kp/tau")
    try:
        identify(*synth_fopdt(du=0.0))
        check(False, "flat duty must raise TuneError")
    except TuneError:
        check(True, "flat duty raises TuneError")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="开环阶跃辨识 + PID 初值")
    ap.add_argument("csv", nargs="?")
    ap.add_argument("--step-time", type=float, help="阶跃发生时刻（s），默认自动检测占空比变化")
    ap.add_argument("--lambda-factor", type=float, default=1.0, help="PI 保守程度，>1 更稳更慢")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if not a.csv:
        ap.print_help()
        return 1
    try:
        t, y, u = load_csv(a.csv)
        m = identify(t, y, u, a.step_time)
    except (TuneError, OSError) as e:
        print(f"错误：{e}")
        return 1
    print(report(m, a.lambda_factor))
    return 0


if __name__ == "__main__":
    sys.exit(main())
