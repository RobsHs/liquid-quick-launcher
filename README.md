# Liquid Quick Launcher (Windhawk Mod)

[![Windhawk Mod](https://img.shields.io/badge/Windhawk-Mod-0078D4?logo=windows&logoColor=white)](https://windhawk.net/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Windows 11](https://img.shields.io/badge/Platform-Windows%2011-blue)](https://www.microsoft.com/windows)

An ultra-smooth **Liquid Glass** productivity launcher for Windows 11 with **Adaptive Frecency Learning**, **120Hz Fluid Animations**, and a **Customizable Outer Window Shape** (ranging from a sharp rectangular box up to a 100% stadium oval pill).

Built natively in C++ / Win32 for [Windhawk](https://windhawk.net/).

---

## ✨ Features

- **⚡ Global Shortcut (`Alt + Space`)**  
  Summons the launcher instantly from anywhere on a dedicated UI thread without ever blocking `explorer.exe`. Press `Esc` or click outside to dismiss.

- **🎨 Sculpted Outer Window & Card Shapes**  
  Dynamically customize both the main window contour and inner selection cards:
  - **Sharp Box (`0px`)**: Crisp, modern rectangular look.
  - **Soft Rounded (`8px`)**: Subtle rounded corners.
  - **Balanced Rounded (`14px`)**: Standard modern Windows 11 styling.
  - **Super Smooth (`26px`)**: Deeply sculpted outer curvature with hardware GDI anti-aliasing.
  - **Full Pill / Oval Capsule (`70px`)**: Morph the entire main search bar into a 100% stadium pill capsule!

- **🧠 Adaptive Smart Prediction & Frecency Engine**  
  Learns your habits automatically. Frequent and recently used apps, commands, or URLs jump straight to #1 with single-letter instant recall (e.g., typing `c` for Chrome or Calculator, `v` for VS Code, `tm` for Task Manager).

- **🚀 120Hz Smooth Animations**  
  - **Fluid Height Expansion**: Main window and capsule contour expand and contract dynamically as results change.
  - **Gliding Selection Capsule**: Active row indicator glides seamlessly between rows.
  - **Fade & Slide Entrance**: Smoothly glides down and fades in when summoned.

- **💎 Curated Liquid Glass Themes**  
  - **Liquid Glass Dark (Obsidian)**
  - **Liquid Glass Light (Soft Alabaster)**
  - **Midnight Pitch Black (AMOLED)**
  - **Nordic Frost (Cool Slate)**

- **🎛️ Transparency Presets**  
  Choose between `55%`, `70%`, `85%` (default), or `100%` solid opacity.

- **🌈 6 Vibrant Accent Colors**  
  Electric Sky Blue, Royal Indigo, Emerald Mint Green, Sunset Rose Pink, Amber Cyber Gold, and Crimson Neon Red.

- **🧮 Integrated Productivity Tools**  
  - **Application Search**: Deep indexer with real 32x32 native application icons.
  - **Terminal Commands**: Prefix with `>` to run any command directly (`cmd.exe`).
  - **Instant Math**: Evaluate expressions on the fly (e.g. `25 * 40`, `sqrt(144)`) and press Enter to copy the result.
  - **System Quick Commands**: `lock`, `sleep`, `restart`, `shutdown`, `empty bin`, `taskmgr`.
  - **Direct URL & Web Search**: Type any web domain (`github.com`) to launch in browser or fallback to Google search.

---

## 📥 Installation

### Method 1: Via Windhawk (Recommended once accepted upstream)
1. Open **Windhawk**.
2. Go to the **Explore** tab.
3. Search for **Liquid Quick Launcher**.
4. Click **Details** and then **Install**.

### Method 2: Manual Installation as Local Mod
1. Open **Windhawk**.
2. Click on the three dots `...` in the top-right corner -> **Advanced** -> **Developer options**.
3. Click **New mod**, set ID as `alt-space-liquid-launcher`.
4. Copy and paste the entire code from [`alt-space-liquid-launcher.wh.cpp`](./alt-space-liquid-launcher.wh.cpp).
5. Click **Save** and then **Compile & Run**.

---

## ⚙️ Settings

You can customize Liquid Quick Launcher at any time from the Windhawk Mod Settings UI:
- **Visual Theme**: Obsidian Dark, Soft Light, Pitch Black, or Nordic Slate.
- **Accent Color**: Electric Blue, Indigo, Mint, Rose, Gold, or Crimson.
- **Glass Transparency**: 55%, 70%, 85%, or 100%.
- **Main Window & Card Shape**: Sharp Box, Soft Round, Balanced Round, Super Smooth, or Pill Capsule.
- **Clean UI Toggles**:
  - Show / Hide `Alt+Space` header badge.
  - Show / Hide item descriptions (subtitles).
  - Show / Hide category badges (`[App]`, `[System]`, `[Web]`, etc.).
  - Show / Hide bottom navigation footer bar.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE) - &copy; RobsHs.
