# A3 — Wall-Clock Methodology & Performance Results

## Executive Summary

This report documents the **A3 — Tightened Wall-Clock Methodology** performance benchmarks for `posixsh` compared against `dash` and `bash`.

To eliminate measurement noise and meet rigorous empirical standards:
1. **Repetitions**: Increased to $N=100$ repetitions per scenario.
2. **Warm-up**: 1 discarded warm-up process launch per scenario before measurement loops to ensure hot page cache state.
3. **Statistical Rigor**: 95% Confidence Intervals ($\text{Mean} \pm 1.96 \times \frac{\sigma}{\sqrt{N}}$), Interquartile Range ($\text{IQR} = Q_3 - Q_1$), Sample Standard Deviation ($\sigma$), and Medians are reported.
4. **Precision**: High-resolution nanosecond timing (`date +%s%N`) computed via an inline Python 3 statistical package.

---

## Environment Specifications

| Attribute | Specification |
|:---|:---|
| **CPU Model** | 11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz |
| **Operating System** | Ubuntu 24.04.2 LTS |
| **Linux Kernel** | 6.17.0-22-generic |
| **Repetitions** | 100 runs (+ 1 discarded warm-up run per scenario) |
| **Timing Method** | Nanosecond precision (`date +%s%N`), inline Python3 stats |

---

## Benchmark Results (N=100)

All wall-clock metrics are reported in **milliseconds (ms)**. Lower is better.

| Scenario | Shell | Median (ms) | Mean ± 95% CI (ms) | IQR [Q1 – Q3] (ms) | Std Dev (ms) | Min – Max (ms) |
|:---|:---|:---:|:---:|:---:|:---:|:---:|
| **Startup + exit** | `posixsh` | **1.99** | 2.39 ± 0.23 | [1.88 – 2.42] | 1.20 | 1.60 – 10.02 |
| | `bash` | 2.91 | 2.95 ± 0.05 | [2.76 – 3.07] | 0.25 | 2.57 – 3.75 |
| | `dash` | 2.30 | 2.36 ± 0.04 | [2.21 – 2.44] | 0.22 | 2.11 – 3.25 |
| **100 pipelines** | `posixsh` | **72.34** | 72.35 ± 0.19 | [71.63 – 72.94] | 0.98 | 69.34 – 75.39 |
| | `bash` | 113.83 | 114.47 ± 0.50 | [112.95 – 114.70] | 2.57 | 111.36 – 125.64 |
| | `dash` | 87.55 | 87.70 ± 0.32 | [87.03 – 88.24] | 1.65 | 85.54 – 100.16 |
| **1 echo (fair)** | `posixsh` | **1.99** | 1.99 ± 0.02 | [1.94 – 2.04] | 0.08 | 1.81 – 2.20 |
| | `bash` | 2.93 | 2.95 ± 0.03 | [2.85 – 3.02] | 0.15 | 2.69 – 3.52 |
| | `dash` | 2.29 | 2.30 ± 0.02 | [2.22 – 2.38] | 0.11 | 2.12 – 2.61 |
| **1 redirect (fair)** | `posixsh` | **2.16** | 2.16 ± 0.02 | [2.11 – 2.22] | 0.10 | 1.94 – 2.52 |
| | `bash` | 2.94 | 2.96 ± 0.02 | [2.87 – 3.07] | 0.12 | 2.71 – 3.28 |
| | `dash` | 2.33 | 2.34 ± 0.02 | [2.25 – 2.40] | 0.10 | 2.13 – 2.59 |
| **100 echo (loop\*)** | `posixsh` | **2.14** | 2.14 ± 0.02 | [2.06 – 2.20] | 0.09 | 1.96 – 2.31 |
| | `bash` | 4.28 | 4.30 ± 0.03 | [4.17 – 4.40] | 0.17 | 3.97 – 4.81 |
| | `dash` | 3.25 | 3.27 ± 0.03 | [3.18 – 3.33] | 0.14 | 3.00 – 3.86 |
| **100 redir (loop\*)** | `posixsh` | 16.26 | 16.24 ± 0.09 | [15.95 – 16.60] | 0.45 | 15.24 – 18.16 |
| | `bash` | 5.21 | 5.39 ± 0.10 | [5.09 – 5.57] | 0.49 | 4.87 – 7.44 |
| | `dash` | **3.85** | 4.16 ± 0.16 | [3.76 – 4.01] | 0.81 | 3.59 – 7.04 |

---

## Methodology & Scenarios

- **Fair Scenarios** (`Startup`, `100 pipelines`, `1 echo`, `1 redirect`): All three shells execute identical `-c` string commands without shell-specific loop constructs.
- **Loop Scenarios** (`100 redir`): `bash` and `dash` use an in-process native `for` loop (single parse step, 0 process forks), whereas `posixsh` processes 100 separate command invocations.

---

## Machine-Readable CSV (`outputs/a3_results.csv`)

```csv
scenario,shell,reps,median_ms,mean_ms,ci_low_ms,ci_high_ms,iqr_low_ms,iqr_high_ms,std_ms,min_ms,max_ms
startup,posixsh,100,1.9926,2.3861,2.1514,2.6207,1.8776,2.4192,1.1973,1.5987,10.0246
startup,bash,100,2.9085,2.9541,2.9050,3.0032,2.7629,3.0701,0.2507,2.5680,3.7540
startup,dash,100,2.2967,2.3620,2.3180,2.4059,2.2069,2.4428,0.2242,2.1126,3.2505
pipelines,posixsh,100,72.3359,72.3459,72.1535,72.5383,71.6320,72.9392,0.9816,69.3393,75.3884
pipelines,bash,100,113.8251,114.4719,113.9675,114.9762,112.9515,114.7007,2.5732,111.3558,125.6353
pipelines,dash,100,87.5509,87.6993,87.3751,88.0234,87.0327,88.2402,1.6539,85.5369,100.1574
echo1,posixsh,100,1.9885,1.9888,1.9732,2.0043,1.9442,2.0383,0.0795,1.8111,2.2001
echo1,bash,100,2.9317,2.9504,2.9203,2.9805,2.8515,3.0209,0.1536,2.6945,3.5176
echo1,dash,100,2.2872,2.2978,2.2769,2.3186,2.2228,2.3756,0.1063,2.1191,2.6113
redir1,posixsh,100,2.1649,2.1619,2.1426,2.1811,2.1108,2.2158,0.0982,1.9350,2.5161
redir1,bash,100,2.9405,2.9578,2.9337,2.9819,2.8748,3.0675,0.1232,2.7084,3.2757
redir1,dash,100,2.3252,2.3369,2.3167,2.3571,2.2545,2.4047,0.1031,2.1330,2.5892
echo100,posixsh,100,2.1387,2.1354,2.1182,2.1527,2.0594,2.1961,0.0881,1.9588,2.3103
echo100,bash,100,4.2793,4.2992,4.2654,4.3329,4.1725,4.4020,0.1722,3.9746,4.8101
echo100,dash,100,3.2548,3.2665,3.2391,3.2940,3.1794,3.3296,0.1399,3.0008,3.8638
redir100,posixsh,100,16.2563,16.2356,16.1473,16.3239,15.9485,16.5964,0.4505,15.2363,18.1600
redir100,bash,100,5.2084,5.3856,5.2902,5.4811,5.0859,5.5703,0.4870,4.8719,7.4426
redir100,dash,100,3.8511,4.1605,4.0016,4.3195,3.7598,4.0126,0.8110,3.5860,7.0394
```
