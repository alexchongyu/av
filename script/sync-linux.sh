#!/usr/bin/env bash
# ─── sync-linux.sh ───────────────────────────────────────────────────────────
# 인터넷 없는 리눅스 서버로 소스 트리를 rsync 동기 + 현재 git 버전을
# VERSION.txt / VERSION_DATE.txt 로 같이 전송한다.
#
# 맥 측에서 실행 전 : 원격 호스트/경로만 확인 (아래 REMOTE 변수)
# 리눅스 측 준비사항: rm -rf .git  (한 번만 — 오래된 태그 제거)
#
# 사용법: bash script/sync-linux.sh
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

REMOTE="${AV_SYNC_REMOTE:-192.168.2.2:/user/alex/claude_code/av/}"

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# ── 현재 버전 스냅샷 ──────────────────────────────────────────────────────────
# 리눅스 쪽 GenVersion.cmake 가 이 두 파일을 읽어 av --version / PDF 에 사용.
git describe --tags --always --dirty --match 'v*' > VERSION.txt
git log -1 --format=%cs                           > VERSION_DATE.txt

VER="$(cat VERSION.txt)"
echo "==> sync to ${REMOTE}  (version: ${VER})"

# ── rsync (변경분만 전송, 빌드 산출물·내부 번들 제외) ─────────────────────────
rsync -avz --delete \
    --exclude='.git/' \
    --exclude='build/' \
    --exclude='bin/' \
    --exclude='.planning/' \
    --exclude='.claude/' \
    --exclude='av-deps.tar.gz' \
    --exclude='av-appimage-tools.tar.gz' \
    --exclude='deps/' \
    --exclude='deps_src/' \
    --exclude='tigervnc-debs/' \
    --exclude='test/' \
    ./ "${REMOTE}"

# ── 산출물은 맥에서 제거 (git 이 진실의 원천) ─────────────────────────────────
rm -f VERSION.txt VERSION_DATE.txt

echo "==> done. 리눅스에서 빌드:"
echo "    cd ~/claude_code/av && ninja -C build && ./bin/av --version"
