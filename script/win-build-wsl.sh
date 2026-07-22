#!/usr/bin/env bash
# ─── win-build-wsl.sh ────────────────────────────────────────────────────────
# Demura(WSL, 192.168.2.241)에서 Windows CMake(VS2022/ClangCL)로 av.exe 빌드.
# 반드시 C: 경로(/mnt/c/Users/Alex/claude_code/av)에서 실행할 것 —
# WSL /home(=\\wsl.localhost\... UNC)에서는 MSBuild/cmd 가 실패한다.
#
# 비대화형 SSH 세션엔 WSL_INTEROP 이 없어 Windows .exe 실행이 안 되므로,
# 살아있는 interop 소켓(사용자 대화형 WSL 세션의 것)을 자동 탐지해 물린다.
#
# 사용법: bash script/win-build-wsl.sh [all|deps|configure|build]
#   deps      : glad 코드 생성기용 jinja2 (Windows Python) 설치
#   configure : cmake configure (VS2022, ClangCL)
#   build     : Release 빌드 → bin/av.exe
#   all(기본) : deps → configure → build
# ──────────────────────────────────────────────────────────────────────────────
set -uo pipefail
cd "$(dirname "$(readlink -f "$0")")/.." || exit 9   # → 리포 루트
echo "BUILD DIR: $(pwd)"

case "$(pwd)" in
  /mnt/c/*) : ;;
  *) echo "!! 경고: C: 경로가 아님 ($(pwd)). WSL /home 에서는 Windows 빌드가 UNC 로 실패합니다." ;;
esac

# 살아있는 WSL interop 소켓 자동 탐지
IEROP=""
for s in /run/WSL/*_interop; do
  if WSL_INTEROP="$s" /mnt/c/Windows/System32/cmd.exe /c ver >/dev/null 2>&1; then IEROP="$s"; break; fi
done
if [ -z "$IEROP" ]; then
  echo "ERROR: 살아있는 WSL interop 소켓 없음 — Demura 에 대화형 WSL 터미널을 하나 열어두세요."
  exit 3
fi
export WSL_INTEROP="$IEROP"
echo "WSL_INTEROP=$IEROP"

CMAKE='/mnt/c/Program Files/CMake/bin/cmake.exe'
PY='/mnt/c/Program Files/Python312/python.exe'
step="${1:-all}"

if [ "$step" = deps ] || [ "$step" = all ]; then
  echo "== PIP DEPS (jinja2 — glad 코드 생성기 필요) =="
  "$PY" -m pip install --user jinja2 2>&1 | tail -5
  "$PY" -c "import jinja2; print('jinja2', jinja2.__version__, 'OK')" || { echo "!! jinja2 설치 실패"; exit 4; }
fi

if [ "$step" = configure ] || [ "$step" = all ]; then
  echo "== CONFIGURE (Visual Studio 17 2022, ClangCL) =="
  rm -rf build
  "$CMAKE" -G "Visual Studio 17 2022" -T ClangCL -B build || { echo "!! CONFIGURE FAILED ($?)"; exit 1; }
  echo "== CONFIGURE OK =="
fi

if [ "$step" = build ] || [ "$step" = all ]; then
  echo "== BUILD (Release) =="
  "$CMAKE" --build build --config Release -j || { echo "!! BUILD FAILED ($?)"; exit 2; }
  echo "== BUILD OK =="
fi

echo "== artifacts =="
ls -la bin/ 2>/dev/null || echo "(no bin/)"
echo "== ALL DONE =="
