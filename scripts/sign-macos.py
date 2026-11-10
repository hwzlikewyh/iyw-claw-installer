"""对已验证的通用安装向导做 Developer ID 签名，可选公证并生成分发包。"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build-signing"
OUTPUT = ROOT / "packages"
NAME = "原助理安装向导"
ZIP_NAME = "YuanAssistant-Setup-macos-universal.zip"
DMG_NAME = "YuanAssistant-Setup-macos-universal.dmg"


def run(*args):
    result = subprocess.run([str(arg) for arg in args], text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{args[0]} failed ({result.returncode}): {result.stderr.strip()}")
    return result.stdout + result.stderr


def prepare_bundle():
    source = WORK / "input" / ZIP_NAME
    expected = os.environ.get("SOURCE_SHA256", "").lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise RuntimeError("Expected source SHA-256 is missing or invalid")
    if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
        raise RuntimeError("Source archive SHA-256 mismatch")
    destination = WORK / "app"
    destination.mkdir(exist_ok=True)
    run("ditto", "-x", "-k", source, destination)
    bundle = destination / f"{NAME}.app"
    executable = bundle / "Contents/MacOS" / NAME
    if set(run("lipo", "-archs", executable).split()) != {"arm64", "x86_64"}:
        raise RuntimeError("Source application is not universal")
    notes = bundle / "Contents/Resources/licenses/使用说明.txt"
    text = notes.read_text(encoding="utf-8").replace(
        "本包是未经过发布证书签名/公证的构建候选，不等同于所有系统的实机验收。",
        "本 macOS 应用使用 Developer ID 正式签名；Apple 公证状态见签名报告，各系统兼容性仍以实机验收为准。")
    notes.write_text(text, encoding="utf-8")
    return bundle


def sign(path):
    run("codesign", "--force", "--timestamp", "--options", "runtime", "--sign",
        os.environ["APPLE_SIGNING_IDENTITY"], "--keychain", os.environ["APPLE_KEYCHAIN_PATH"], path)
    run("codesign", "--verify", "--deep", "--strict", path)
    detail = run("codesign", "-d", "--verbose=4", path)
    if "Authority=Developer ID Application:" not in detail or "Timestamp=" not in detail:
        raise RuntimeError("Missing Developer ID authority or secure timestamp")
    return detail


def notarize(artifact, staple_targets):
    credentials = [os.environ.get(key, "") for key in ("APPLE_ID", "APPLE_PASSWORD", "APPLE_TEAM_ID")]
    if not any(credentials):
        return {"status": "not-submitted", "reason": "Apple notarization credentials were not provided"}
    if not all(credentials):
        raise RuntimeError("Incomplete Apple notarization credentials")
    profile = "yuan-installer-notarization"
    keychain = os.environ["APPLE_KEYCHAIN_PATH"]
    run("xcrun", "notarytool", "store-credentials", profile, "--keychain", keychain,
        "--apple-id", credentials[0], "--password", credentials[1], "--team-id", credentials[2])
    result = json.loads(run("xcrun", "notarytool", "submit", artifact, "--keychain-profile", profile,
                           "--keychain", keychain, "--wait", "--timeout", "15m", "--output-format", "json"))
    if result.get("status") != "Accepted":
        raise RuntimeError(f"Apple notarization did not accept the file: {result.get('id')} {result.get('status')}")
    for target in staple_targets:
        run("xcrun", "stapler", "staple", target)
        run("xcrun", "stapler", "validate", target)
    return {"status": result["status"], "submissionId": result["id"]}


def write_checksums():
    lines = []
    for artifact in sorted(OUTPUT.iterdir()):
        if artifact.suffix not in {".zip", ".dmg"}:
            continue
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
        lines.append(f"{digest}  {artifact.name}")
    (OUTPUT / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    OUTPUT.mkdir(exist_ok=True)
    bundle = prepare_bundle()
    detail = sign(bundle)
    archive = OUTPUT / ZIP_NAME
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", bundle, archive)
    app_notary = notarize(archive, [bundle])
    if app_notary["status"] == "Accepted":
        archive.unlink()
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", bundle, archive)
        run("spctl", "--assess", "--type", "execute", "--verbose=2", bundle)
    image = OUTPUT / DMG_NAME
    run("hdiutil", "create", "-volname", NAME, "-srcfolder", bundle.parent,
        "-format", "UDZO", image)
    dmg_detail = sign(image)
    dmg_notary = notarize(image, [image])
    report = {"appSignature": detail, "dmgSignature": dmg_detail,
              "appNotarization": app_notary, "dmgNotarization": dmg_notary}
    (OUTPUT / "macos-signing-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    write_checksums()
    print(f"Developer ID signatures verified; notarization: {app_notary['status']}")


if __name__ == "__main__":
    main()
