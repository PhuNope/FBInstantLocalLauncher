# FB Instant Games Local Launcher

Native Qt 6 desktop utility for Windows to manage locally built Cocos Creator Facebook Instant Games and open them with Facebook Embedded Player.

## Features

- Add, edit, delete game entries (name, Facebook App ID, Cocos build path, HTTPS port).
- Play starts a local HTTPS process and opens `https://www.facebook.com/embed/instantgames/APP_ID/player?game_url=https%3A%2F%2Flocalhost%3A8080`.
- Stop the local server.
- All games persist as JSON under Windows `%APPDATA%/FBInstantTools/FBInstantLocalLauncher/games.json` (actual location can vary by Qt platform settings).
- Entries are never uploaded, and deleting an entry never deletes a build directory.

## Requirements

- CLion with CMake 3.21+, C++17 compiler (MSVC recommended).
- Qt 6.5+ **Widgets + Network**, with matching kit/compiler.
- Node.js/npm; run `npm install -g http-server` and ensure the command is on `PATH`.
- OpenSSL for generating local HTTPS certificates.

## CLion / CMake build

Configure a CMake profile with Qt's installation prefix, e.g. `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`, and use a matching MSVC toolchain. Open the folder in CLion and build target `FBInstantLocalLauncher`.

Alternatively:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

For distributable builds, run `windeployqt` on the exe to copy Qt runtime DLLs (the GitHub workflow does this automatically).

## Prepare a game

1. In Cocos Creator 3.8.8, build **Facebook Instant Games** and find the directory with `index.html`.
2. In that directory, create a local HTTPS certificate using OpenSSL. For modern Chrome, include SAN `DNS:localhost`:

```powershell
openssl req -x509 -newkey rsa:2048 -sha256 -nodes -keyout key.pem -out cert.pem -days 365 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

3. Visit `https://localhost:8080` after Play and explicitly trust the self-signed certificate **only on your own machine** (or import it into your local trust store). Facebook may still block untrusted localhost certificates, iframe embedding or the `game_url` override depending on the browser and current Meta app configuration.
4. Add the game with its real numeric Facebook App ID, build folder path, and desired local port. Select it and click **Play**.

> Every Play switches the launcher-managed server to the chosen game and port. This only stops processes started by the launcher. If port is occupied by another service, choose another port or stop that service.

> The `game_url` parameter and embedded player are dependent on Meta platform behavior and access permissions. Successful server startup does not guarantee Facebook will embed the game. Inspect DevTools Network and iframe contexts when debugging.

## Git branches and releases

Recommended strategy:

- `dev`: daily development and feature PRs.
- `main`: stable production/release branch.
- Merge `dev` into `main` with a PR, then create an annotated tag on the `main` HEAD:

```bash
git checkout -b dev
git push -u origin dev
# develop, commit and merge dev -> main in GitHub

git checkout main
git pull origin main
git tag -a v1.0.0 -m "Release 1.0.0"
git push origin v1.0.0
```

Tag pushes matching `v*` trigger `.github/workflows/release.yml`. The workflow verifies the tag commit is an ancestor of `origin/main`, builds the Windows x64 app, runs `windeployqt` and publishes the ZIP in **GitHub Releases**. The ancestry test allows tags on older main commits; for stricter "HEAD only" replace the verification with an exact SHA equality check.

In GitHub repository settings → Actions → General → Workflow permissions, allow **Read and write permissions** if your repository policy requires it. Use branch protection to restrict merges and tag creation as appropriate.

## Security and limitations

- Certificates and private keys are excluded via `.gitignore`; never commit them.
- Launcher starts a locally installed `http-server` command. It is not bundled with released ZIP and requires installation on user PCs.
- This is a local development helper, not a production deployment server.
- JSON data stays on the user's own machine, not in Git.


## Updates (v1.1.0)

- On app startup, check `GET https://api.github.com/repos/OWNER/REPO/releases/latest`.
- Set the public GitHub repository in **Updates settings** (`owner/repo`). When built in GitHub Actions, the repository is compiled into the app by `-DLAUNCHER_GITHUB_REPO=owner/repo`.
- If latest release semver is newer, show a confirmation popup. **Yes** downloads `FBInstantLocalLauncher-vX.Y.Z-windows-x64.zip` and launches `updater.ps1` in a separate PowerShell process. App closes, updater waits, extracts and copies files, then restarts app.
- Run from a **writable, extracted folder**, not inside a zip and not a protected `Program Files` folder without write permission. No admin elevation is attempted.
- Releases and ZIPs must be **public**. Latest endpoint excludes prereleases and drafts. Updates require trusted GitHub repository access and are not code-signed. Use releases from a repo you control.
- Saved games: `%APPDATA%` via Qt `AppDataLocation`, not next to the EXE; updates preserve game list.
- **Check updates** button checks manually. Startup checks are silent when offline.

### Local build (CLion / Windows)

Set CMake option `-DLAUNCHER_GITHUB_REPO=OWNER/REPO` and your Qt path.
The build copies `updater.ps1` beside `FBInstantLocalLauncher.exe`. For portable distribution, run `windeployqt` in the output folder or use GitHub Releases package.

### Release

Commit to `dev`, merge into `main`, then: `git checkout main && git pull && git tag v1.1.0 && git push origin v1.1.0`.
Workflow checks that tag commit is reachable from `origin/main` and builds an x64 EXE with Qt runtime in ZIP. It publishes a release at the tag. The EXE is **inside the ZIP**, not directly installable on its own.

> Note: This repo does not ship a prebuilt Windows executable from the current authoring environment. The GitHub Actions Windows build produces it when the workflow succeeds.

### Test builds on every push

`.github/workflows/windows-test.yml` builds Windows x64 on pushes to `dev` or `main`, or manual **Actions → Windows test build → Run workflow**. Download artifact `FBInstantLocalLauncher-windows-x64-test` from that workflow run; it includes `FBInstantLocalLauncher.exe`, updater script, and Qt dependencies. These builds do **not** create a Release.
