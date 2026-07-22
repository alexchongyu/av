#!/usr/bin/env bash
# ─── sync-win.sh ─────────────────────────────────────────────────────────────
# Windows 빌드 머신(Demura, WSL)으로 소스 트리를 rsync 동기 + 현재 git 버전을
# VERSION.txt / VERSION_DATE.txt 로 같이 전송한다.
# 대상 WSL 경로에서 Windows CMake(VS2022/ClangCL)로 av.exe 를 빌드한다.
#
# 사전조건(맥): 192.168.2.241 무패스워드 SSH (~/.ssh/config: Host 192.168.2.241)
# 사용법:       bash script/sync-win.sh
# 빌드(241):    cd ~/claude_code/av
#               "/mnt/c/Program Files/CMake/bin/cmake.exe" -G "Visual Studio 17 2022" -T ClangCL -B build
#               "/mnt/c/.../devenv.com" build/av.sln /build Release   (= wcmake / wmake)
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

# Windows 파일시스템(C:) 경로에 두어야 VS 툴체인이 빌드 가능.
# (WSL /home = \\wsl.localhost\... UNC 라 MSBuild/cmd 가 거부함)
REMOTE="${AV_WIN_REMOTE:-192.168.2.241:/mnt/c/Users/Alex/claude_code/av/}"

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# ── 현재 버전 스냅샷 (GenVersion.cmake 가 읽어 av --version / PDF 에 사용) ──────
git describe --tags --always --dirty --match 'v*' > VERSION.txt
git log -1 --format=%cs                           > VERSION_DATE.txt

VER="$(cat VERSION.txt)"
echo "==> sync to ${REMOTE}  (version: ${VER})"

# ── rsync (변경분만, 빌드 산출물·내부 번들·플랫폼 무관 파일 제외) ──────────────
rsync -avz --delete \
    --exclude='.git/' \
    --exclude='build/' \
    --exclude='build-*/' \
    --exclude='bin/' \
    --exclude='.planning/' \
    --exclude='.claude/' \
    --exclude='av-deps.tar.gz' \
    --exclude='av-appimage-tools.tar.gz' \
    --exclude='deps/' \
    --exclude='deps_src/' \
    --exclude='tigervnc-debs/' \
    --exclude='test/' \
    --exclude='.DS_Store' \
    ./ "${REMOTE}"

# ── 산출물은 맥에서 제거 (git 이 진실의 원천) ─────────────────────────────────
rm -f VERSION.txt VERSION_DATE.txt

echo "==> done. Demura(WSL)에서 빌드 (C: 경로):"
echo "    ssh 192.168.2.241"
echo "    cd /mnt/c/Users/Alex/claude_code/av && wcmake -B build && cmake --build build --config Release"
echo "    → bin/av.exe"
