#!/usr/bin/env bash
# ─── win-install.sh ──────────────────────────────────────────────────────────
# Demura(WSL, 192.168.2.241)에서 av.exe를 세 위치에 설치한다:
#   1) %LOCALAPPDATA%\av\av.exe   — 승격 불필요 (cp)
#   2) C:\Windows\av.exe          — UAC 팝업 없이, 관리자 스케줄드 태스크 경유
#
# 사전조건:
#   - bin/av.exe 빌드 완료 (script/win-build-wsl.sh build)
#   - 관리자 태스크 av_sys_install 등록(1회, 관리자 cmd):
#       schtasks /Create /TN av_sys_install ^
#         /TR "C:\Users\Alex\claude_code\av\install-sys.cmd" ^
#         /SC ONCE /ST 00:00 /RL HIGHEST /F
#
# install-sys.cmd는 sync-win.sh(rsync --delete)가 매번 지우므로 여기서 재생성한다.
# 검증은 태스크가 남기는 install-sys.log(copy_exit=0)와 md5 비교로 한다.
#
# 사용법(맥에서):
#   ssh 192.168.2.241 'bash -lc "cd /mnt/c/Users/Alex/claude_code/av && bash script/win-install.sh"'
# ──────────────────────────────────────────────────────────────────────────────
set -uo pipefail
cd "$(dirname "$(readlink -f "$0")")/.." || exit 9

BIN=bin/av.exe
[ -f "$BIN" ] || { echo "ERROR: $BIN 없음 — 먼저 script/win-build-wsl.sh build"; exit 2; }

# 살아있는 WSL interop 소켓 (Windows .exe 실행용; 죽은 소켓에서 안 멈추게 timeout)
IEROP=""
for s in /run/WSL/*_interop; do
  if timeout 5 env WSL_INTEROP="$s" /mnt/c/Windows/System32/cmd.exe /c ver >/dev/null 2>&1; then
    IEROP="$s"; break
  fi
done
[ -z "$IEROP" ] && { echo "ERROR: live interop 소켓 없음 — Demura에 WSL 터미널을 하나 열어두세요"; exit 3; }
export WSL_INTEROP="$IEROP"

# 1) LOCALAPPDATA (승격 불필요)
mkdir -p /mnt/c/Users/Alex/AppData/Local/av
cp -f "$BIN" /mnt/c/Users/Alex/AppData/Local/av/av.exe

# 2) C:\Windows — 관리자 태스크 경유 (UAC 없음). .cmd는 항상 갓 생성.
printf 'copy /Y "C:\\Users\\Alex\\claude_code\\av\\bin\\av.exe" "C:\\Windows\\av.exe"\r\necho %%DATE%% %%TIME%% copy_exit=%%ERRORLEVEL%% > "C:\\Users\\Alex\\claude_code\\av\\install-sys.log"\r\n' \
  > install-sys.cmd
rm -f install-sys.log
if ! /mnt/c/Windows/System32/schtasks.exe /Run /TN av_sys_install >/dev/null 2>&1; then
  echo "ERROR: schtasks /Run 실패 — av_sys_install 태스크 등록 여부를 확인하세요"
  exit 4
fi
for _ in $(seq 1 10); do [ -f install-sys.log ] && break; sleep 1; done

# 3) 검증: 태스크 로그 + md5 3위치 비교
echo "── install-sys.log ──"
cat install-sys.log 2>/dev/null || echo "(로그 없음 — 태스크가 실행되지 않음?)"
echo "── md5 ──"
md5sum "$BIN" /mnt/c/Users/Alex/AppData/Local/av/av.exe /mnt/c/Windows/av.exe
m_src=$(md5sum "$BIN" | cut -d' ' -f1)
m_sys=$(md5sum /mnt/c/Windows/av.exe | cut -d' ' -f1)
if [ "$m_src" = "$m_sys" ]; then
  echo "OK: C:\\Windows\\av.exe 최신 (UAC 없이 설치 완료)"
else
  echo "FAIL: C:\\Windows\\av.exe 가 소스와 불일치"
  exit 5
fi
